// Composite fonts (ISO 32000-1 section 9.7; Adobe Technical Note 5014; D01 T02 section 30).
#include "font_cid.h"

#include "cmap_lexer.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <iterator>
#include <map>
#include <new>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

namespace recto::pdf {

namespace {

[[nodiscard]] PdfError cid_error(ErrorCode code, std::string_view message) noexcept
{
	return make_pdf_error(code, message);
}

// A run of keys from `low` to `high` and its value at `low`; the value at a later key comes from `shift(value, delta)`.
template <typename V>
struct Span {
	uint64_t low;
	uint64_t high;
	V value;
};

// Lays `add` over the spans of `set` (keyed by their first key, not overlapping), trimming and splitting what it
// covers, so the later span wins where they overlap.
template <typename V, typename Shift>
void lay_over(std::map<uint64_t, Span<V>>& set, const Span<V>& add, Shift shift)
{
	auto it = set.upper_bound(add.low);
	if (it != set.begin()) {
		const auto before = std::prev(it);
		if (before->second.high >= add.low) {
			it = before;
		}
	}
	while (it != set.end() && it->second.low <= add.high) {
		const Span<V> old = it->second;
		it = set.erase(it);
		if (old.low < add.low) {
			set.emplace(old.low, Span<V>{old.low, add.low - 1u, old.value});
		}
		if (old.high > add.high) {
			set.emplace(add.high + 1u, Span<V>{add.high + 1u, old.high, shift(old.value, add.high + 1u - old.low)});
		}
	}
	set.emplace(add.low, add);
}

// The span holding `key` in a sorted, non-overlapping list, or null.
template <typename S>
[[nodiscard]] const S* find_span(const std::vector<S>& spans, uint64_t key, uint64_t (*low_of)(const S&)) noexcept
{
	auto it = std::upper_bound(spans.begin(), spans.end(), key, [&](uint64_t k, const S& s) { return k < low_of(s); });
	if (it == spans.begin()) {
		return nullptr;
	}
	--it;
	return &*it;
}

[[nodiscard]] uint64_t code_key(uint8_t length, uint32_t code) noexcept
{
	return (uint64_t{length} << 32u) | code;
}

[[nodiscard]] std::optional<int64_t> integer_of(std::string_view text) noexcept
{
	int64_t value = 0;
	const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (ec != std::errc{} || end != text.data() + text.size()) {
		return std::nullopt;
	}
	return value;
}

using CMapSpans = std::map<uint64_t, Span<uint32_t>>;

[[nodiscard]] uint32_t shift_cid(uint32_t cid, uint64_t delta) noexcept
{
	return static_cast<uint32_t>(cid + delta);
}

[[nodiscard]] uint32_t same_cid(uint32_t cid, uint64_t) noexcept
{
	return cid;
}

void load_spans(CMapSpans& set, const std::vector<CidCMap::Mapping>& mappings)
{
	for (const CidCMap::Mapping& m : mappings) {
		set.emplace(code_key(m.length, m.low), Span<uint32_t>{code_key(m.length, m.low), code_key(m.length, m.high), m.cid});
	}
}

[[nodiscard]] std::vector<CidCMap::Mapping> spans_to_mappings(const CMapSpans& set)
{
	std::vector<CidCMap::Mapping> out;
	out.reserve(set.size());
	for (const auto& [key, span] : set) {
		out.push_back(CidCMap::Mapping{static_cast<uint32_t>(span.low), static_cast<uint32_t>(span.high),
			static_cast<uint8_t>(span.low >> 32u), span.value});
	}
	return out;
}

[[nodiscard]] uint64_t mapping_low(const CidCMap::Mapping& m) noexcept
{
	return code_key(m.length, m.low);
}

// Each byte of the first `length` bytes within the range's byte at that place (Technical Note 5014).
[[nodiscard]] bool in_codespace(const CidCMap::Range& range, std::span<const uint8_t> bytes, uint8_t bytes_checked) noexcept
{
	for (uint8_t i = 0; i < bytes_checked; ++i) {
		const uint32_t shift = 8u * (range.length - 1u - i);
		const uint32_t b = bytes[i];
		if (b < ((range.low >> shift) & 0xFFu) || b > ((range.high >> shift) & 0xFFu)) {
			return false;
		}
	}
	return true;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// CidCMap
// ---------------------------------------------------------------------------------------------------------------------

CidCMap CidCMap::identity(bool vertical) noexcept
{
	CidCMap cmap;
	cmap.identity_ = true;
	cmap.vertical_ = vertical;
	return cmap;
}

PdfResult<CidCMap> CidCMap::parse(std::span<const uint8_t> stream, const CidCMap* base) noexcept
{
	try {
		if (stream.size() > kMaxCidCMapStreamBytes) {
			return cid_error(ErrorCode::ResourceLimit, "CID CMap stream exceeds its size limit");
		}
		CidCMap cmap;
		CMapSpans cids;
		CMapSpans notdefs;
		// A CMap has one base: the `/UseCMap` CMap passed in, or Identity through `usecmap` (Technical Note 5014); an
		// Identity `usecmap` over an Identity base is the same base, any other second base is damage.
		bool based = false;
		bool identity_based = false;
		const auto seed_identity = [&]() -> PdfResult<void> {
			if (based) {
				if (identity_based) {
					return {};
				}
				return cid_error(ErrorCode::DamagedFormat, "CID CMap names a second base CMap");
			}
			if (cmap.codespace_.size() >= kMaxCidCodespaceRanges) {
				return cid_error(ErrorCode::ResourceLimit, "CID CMap has more codespace ranges than its limit");
			}
			based = true;
			identity_based = true;
			cmap.codespace_.push_back(Range{0u, 0xFFFFu, 2u});
			lay_over(cids, Span<uint32_t>{code_key(2u, 0u), code_key(2u, 0xFFFFu), 0u}, shift_cid);
			return {};
		};
		if (base != nullptr) {
			cmap.vertical_ = base->vertical_;
			if (base->identity_) {
				RECTO_PDF_RETURN_IF_ERROR(seed_identity());
			} else {
				based = true;
				cmap.codespace_ = base->codespace_;
				load_spans(cids, base->cids_);
				load_spans(notdefs, base->notdefs_);
			}
		}
		CMapLexer lexer{stream}; // hex strings at `kMaxToUnicodeTargetBytes`; codes are held to 4 bytes below
		size_t entries = 0;
		const auto count_entry = [&]() -> PdfResult<void> {
			if (++entries > kMaxCidMappings) {
				return cid_error(ErrorCode::ResourceLimit, "CID CMap holds more mappings than its limit");
			}
			return {};
		};
		const auto code_of = [](const CMapLexer::Token& token) -> std::optional<std::pair<uint32_t, uint8_t>> {
			if (token.kind != CMapLexer::Kind::Hex || token.hex.empty() || token.hex.size() > 4u) {
				return std::nullopt;
			}
			uint32_t code = 0;
			for (const char c : token.hex) {
				code = (code << 8u) | static_cast<uint8_t>(c);
			}
			return std::pair{code, static_cast<uint8_t>(token.hex.size())};
		};
		const auto cid_of = [](const CMapLexer::Token& token) -> std::optional<int64_t> {
			if (token.kind != CMapLexer::Kind::Keyword) {
				return std::nullopt;
			}
			return integer_of(token.text);
		};
		const auto malformed = [](std::string_view what) { return cid_error(ErrorCode::DamagedFormat, what); };
		// A CID mapping's codes take consecutive CIDs, a notdef mapping's one CID for them all; the rule belongs to the
		// set, so a span split by a later mapping keeps it.
		const auto add = [&](bool notdef, uint32_t low, uint32_t high, uint8_t length, int64_t cid) -> PdfResult<void> {
			CMapSpans& set = notdef ? notdefs : cids;
			const bool increments = !notdef;
			if (cid < 0) {
				return malformed("CID CMap maps a code to a negative CID");
			}
			const uint64_t last = static_cast<uint64_t>(cid) + (increments ? uint64_t{high} - low : 0u);
			if (last > kMaxCid) {
				return cid_error(ErrorCode::ResourceLimit, "CID CMap maps a code past CID 65,535");
			}
			RECTO_PDF_RETURN_IF_ERROR(count_entry());
			lay_over(set, Span<uint32_t>{code_key(length, low), code_key(length, high), static_cast<uint32_t>(cid)},
				increments ? shift_cid : same_cid);
			return {};
		};
		CMapLexer::Token previous;
		for (;;) {
			RECTO_PDF_TRY(CMapLexer::Token token, lexer.next());
			if (token.kind == CMapLexer::Kind::End) {
				break;
			}
			if (token.kind == CMapLexer::Kind::Name && token.text == "CMapName") {
				RECTO_PDF_TRY(CMapLexer::Token cmap_name, lexer.next());
				if (cmap_name.kind == CMapLexer::Kind::Name) {
					cmap.name_ = std::string(cmap_name.text);
				}
				continue;
			}
			if (token.kind == CMapLexer::Kind::Name && token.text == "WMode") {
				RECTO_PDF_TRY(CMapLexer::Token mode, lexer.next());
				if (const auto value = cid_of(mode)) {
					cmap.vertical_ = *value == 1;
				}
				continue;
			}
			if (token.kind != CMapLexer::Kind::Keyword) {
				previous = std::move(token);
				continue;
			}
			if (token.text == "usecmap") {
				if (previous.kind == CMapLexer::Kind::Name && (previous.text == "Identity-H" || previous.text == "Identity-V")) {
					RECTO_PDF_RETURN_IF_ERROR(seed_identity());
					continue;
				}
				// The base the stream's `/UseCMap` already gave, named by its `/CMapName` (or unnamed), is that base.
				if (previous.kind == CMapLexer::Kind::Name && base != nullptr && !base->identity_ &&
					(base->name_.empty() || base->name_ == previous.text)) {
					continue;
				}
				return cid_error(ErrorCode::UnsupportedFeature, "CID CMap uses a CMap other than Identity or its base");
			}
			const bool codespace = token.text == "begincodespacerange";
			const bool range = token.text == "begincidrange" || token.text == "beginnotdefrange";
			const bool single = token.text == "begincidchar" || token.text == "beginnotdefchar";
			if (!codespace && !range && !single) {
				previous = std::move(token);
				continue;
			}
			const bool notdef = token.text == "beginnotdefrange" || token.text == "beginnotdefchar";
			const std::string_view end = codespace ? "endcodespacerange"
				: range ? (notdef ? "endnotdefrange" : "endcidrange")
						: (notdef ? "endnotdefchar" : "endcidchar");
			for (;;) {
				RECTO_PDF_TRY(CMapLexer::Token first, lexer.next());
				if (first.kind == CMapLexer::Kind::Keyword && first.text == end) {
					break;
				}
				if (first.kind == CMapLexer::Kind::End) {
					return malformed("CID CMap block is not closed");
				}
				const auto lo = code_of(first);
				if (codespace || range) {
					RECTO_PDF_TRY(CMapLexer::Token second, lexer.next());
					const auto hi = code_of(second);
					if (!lo.has_value() || !hi.has_value() || lo->second != hi->second || lo->first > hi->first) {
						return malformed("CID CMap range bounds are malformed");
					}
					if (codespace) {
						if (cmap.codespace_.size() >= kMaxCidCodespaceRanges) {
							return cid_error(ErrorCode::ResourceLimit, "CID CMap has more codespace ranges than its limit");
						}
						cmap.codespace_.push_back(Range{lo->first, hi->first, lo->second});
						continue;
					}
					RECTO_PDF_TRY(CMapLexer::Token dst, lexer.next());
					const auto cid = cid_of(dst);
					if (!cid.has_value()) {
						return malformed("CID CMap range has no CID");
					}
					RECTO_PDF_RETURN_IF_ERROR(add(notdef, lo->first, hi->first, lo->second, *cid));
				} else {
					RECTO_PDF_TRY(CMapLexer::Token dst, lexer.next());
					const auto cid = cid_of(dst);
					if (!lo.has_value() || !cid.has_value()) {
						return malformed("CID CMap single mapping is malformed");
					}
					RECTO_PDF_RETURN_IF_ERROR(add(notdef, lo->first, lo->first, lo->second, *cid));
				}
			}
		}
		if (cmap.codespace_.empty()) {
			return malformed("CID CMap has no codespace range");
		}
		cmap.cids_ = spans_to_mappings(cids);
		cmap.notdefs_ = spans_to_mappings(notdefs);
		return cmap;
	} catch (const std::bad_alloc&) {
		return cid_error(ErrorCode::OutOfMemory, "Out of memory reading a CID CMap");
	}
}

CidCode CidCMap::next(std::span<const uint8_t> bytes) const noexcept
{
	CidCode out;
	if (bytes.empty()) {
		return out;
	}
	if (identity_) {
		if (bytes.size() < 2u) {
			return CidCode{bytes[0], 1u, 0u};
		}
		const uint32_t code = (uint32_t{bytes[0]} << 8u) | bytes[1];
		return CidCode{code, 2u, code};
	}
	// The shortest codespace range the bytes match, by length.
	uint8_t matched = 0;
	for (uint8_t length = 1; length <= 4u && matched == 0u; ++length) {
		if (bytes.size() < length) {
			break;
		}
		for (const Range& range : codespace_) {
			if (range.length == length && in_codespace(range, bytes, length)) {
				matched = length;
				break;
			}
		}
	}
	uint8_t length = matched;
	if (matched == 0u) {
		// No match: the shortest range whose first byte matches, else the shortest range (9.7.6.3).
		uint8_t by_first = 0;
		uint8_t shortest = 4u;
		for (const Range& range : codespace_) {
			shortest = std::min(shortest, range.length);
			if (in_codespace(range, bytes, 1u) && (by_first == 0u || range.length < by_first)) {
				by_first = range.length;
			}
		}
		length = by_first != 0u ? by_first : shortest;
	}
	const auto taken = static_cast<uint8_t>(std::min<size_t>(length, bytes.size()));
	uint32_t code = 0;
	for (uint8_t i = 0; i < taken; ++i) {
		code = (code << 8u) | bytes[i];
	}
	out.code = code;
	out.length = taken;
	if (matched == 0u) {
		return out; // CID 0
	}
	const uint64_t key = code_key(length, code);
	if (const Mapping* m = find_span(notdefs_, key, mapping_low); m != nullptr && key <= code_key(m->length, m->high)) {
		out.notdef_cid = m->cid;
	}
	if (const Mapping* m = find_span(cids_, key, mapping_low); m != nullptr && key <= code_key(m->length, m->high)) {
		out.cid = m->cid + (code - m->low);
		return out;
	}
	out.cid = out.notdef_cid;
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// CidToGidMap and the glyph a CID selects
// ---------------------------------------------------------------------------------------------------------------------

CidToGidMap CidToGidMap::identity() noexcept
{
	return CidToGidMap{};
}

CidToGidMap CidToGidMap::from_glyphs(std::vector<uint16_t> glyphs) noexcept
{
	CidToGidMap map;
	map.identity_ = false;
	map.glyphs_ = std::move(glyphs);
	return map;
}

PdfResult<CidToGidMap> CidToGidMap::parse(std::span<const uint8_t> stream) noexcept
{
	try {
		if (stream.size() % 2u != 0u) {
			return cid_error(ErrorCode::DamagedFormat, "CIDToGIDMap stream has an odd length");
		}
		CidToGidMap map;
		map.identity_ = false;
		const size_t count = std::min<size_t>(stream.size() / 2u, size_t{kMaxCid} + 1u);
		map.glyphs_.resize(count);
		for (size_t i = 0; i < count; ++i) {
			map.glyphs_[i] = static_cast<uint16_t>((stream[2u * i] << 8u) | stream[2u * i + 1u]);
		}
		return map;
	} catch (const std::bad_alloc&) {
		return cid_error(ErrorCode::OutOfMemory, "Out of memory reading a CIDToGIDMap");
	}
}

uint32_t CidToGidMap::glyph(uint32_t cid) const noexcept
{
	if (identity_) {
		return cid;
	}
	return cid < glyphs_.size() ? glyphs_[cid] : 0u;
}

uint16_t cid_glyph(uint32_t cid, CidFontKind kind, const CidToGidMap& map, const LoadedFont* font) noexcept
{
	uint32_t glyph = cid;
	if (kind == CidFontKind::CIDFontType2) {
		glyph = map.glyph(cid);
	} else if (font != nullptr && font->cid_keyed()) {
		glyph = font->glyph_for_cid(cid).value_or(0u);
	} else {
		glyph = map.glyph(cid); // the charset of an OpenType-wrapped CID-keyed CFF, else the identity
	}
	if (glyph > 0xFFFFu || (font != nullptr && glyph >= font->glyph_count())) {
		return 0u;
	}
	return static_cast<uint16_t>(glyph);
}

// ---------------------------------------------------------------------------------------------------------------------
// CidWidths
// ---------------------------------------------------------------------------------------------------------------------

namespace {

// A resolved value's keeper: the document's shared value when a shared resolver gave one, else an owned copy.
struct Held {
	PdfValue owned{};
	std::shared_ptr<const PdfValue> shared{};
};

// A value with its indirect reference resolved, or null when it is absent, unresolvable or there is no resolver.
// Allocation failure is the one resolver error passed on.
// Through `shared` when given, the document's cached value is held without a copy (D01 T01 section 11).
[[nodiscard]] PdfResult<const PdfValue*> deref(const PdfValue* value, Held& holder, const IndirectObjectResolver* resolver,
	const SharedObjectResolver* shared = nullptr) noexcept
{
	if (value == nullptr || !value->is_ref()) {
		return value;
	}
	if (shared != nullptr && *shared) {
		auto resolved = (*shared)(*value->as_ref());
		if (!resolved.has_value()) {
			if (resolved.error().code != ErrorCode::DamagedFormat) {
				return resolved.error();
			}
			return static_cast<const PdfValue*>(nullptr);
		}
		holder.shared = std::move(*resolved);
		return static_cast<const PdfValue*>(holder.shared == nullptr || holder.shared->is_ref() ? nullptr : holder.shared.get());
	}
	if (resolver == nullptr || !*resolver) {
		return static_cast<const PdfValue*>(nullptr);
	}
	auto resolved = (*resolver)(*value->as_ref());
	if (!resolved.has_value()) {
		if (resolved.error().code != ErrorCode::DamagedFormat) {
			return resolved.error(); // a limit, allocation failure, cancellation or I/O error is not a malformed entry
		}
		return static_cast<const PdfValue*>(nullptr);
	}
	holder.owned = std::move(*resolved);
	return static_cast<const PdfValue*>(holder.owned.is_ref() ? nullptr : &holder.owned);
}

[[nodiscard]] std::optional<uint32_t> cid_value(const PdfValue* value) noexcept
{
	if (value == nullptr) {
		return std::nullopt;
	}
	const std::optional<int64_t> n = value->as_int();
	if (!n.has_value() || *n < 0 || *n > int64_t{0xFFFFFFFF}) {
		return std::nullopt;
	}
	return static_cast<uint32_t>(*n);
}

[[nodiscard]] PdfError oversized_range() noexcept
{
	return cid_error(ErrorCode::ResourceLimit, "CIDFont width range reaches past CID 65,535");
}

// Walks a `/W` or `/W2` array: `c [values...]` gives each CID from `c` a group of `group` numbers, `c_first c_last
// values` gives the CIDs of the range one group; a malformed entry is skipped.
template <typename OnRun>
[[nodiscard]] PdfResult<void> walk_widths(
	const PdfArray& array, size_t group, const IndirectObjectResolver* resolver, const SharedObjectResolver* shared, OnRun on_run)
{
	size_t elements = array.size();
	const auto counted = [&](size_t more) -> PdfResult<void> {
		elements += more;
		if (elements > kMaxCidWidthElements) {
			return cid_error(ErrorCode::ResourceLimit, "CIDFont width array holds more elements than its limit");
		}
		return {};
	};
	RECTO_PDF_RETURN_IF_ERROR(counted(0u));
	std::array<double, 3> values{};
	const auto read_group = [&](const PdfArray& from, size_t at, Held* holders) -> PdfResult<bool> {
		for (size_t k = 0; k < group; ++k) {
			RECTO_PDF_TRY(const PdfValue* v, deref(&from[at + k], holders[k], resolver, shared));
			const std::optional<double> n = v != nullptr ? v->as_number() : std::nullopt;
			if (!n.has_value()) {
				return false;
			}
			values[k] = *n / 1000.0;
		}
		return true;
	};
	size_t i = 0;
	while (i < array.size()) {
		Held first_holder;
		RECTO_PDF_TRY(const PdfValue* first_value, deref(&array[i], first_holder, resolver, shared));
		const std::optional<uint32_t> first = cid_value(first_value);
		if (!first.has_value() || i + 1u >= array.size()) {
			++i; // not a CID: skipped
			continue;
		}
		Held second_holder;
		RECTO_PDF_TRY(const PdfValue* second, deref(&array[i + 1u], second_holder, resolver, shared));
		if (second != nullptr && second->is_array()) {
			const PdfArray& list = *second->as_array();
			RECTO_PDF_RETURN_IF_ERROR(counted(list.size()));
			const size_t groups = list.size() / group;
			if (groups > 0u && uint64_t{*first} + groups - 1u > kMaxCid) {
				return oversized_range();
			}
			for (size_t g = 0; g < groups; ++g) {
				std::array<Held, 3> holders{};
				RECTO_PDF_TRY(const bool ok, read_group(list, g * group, holders.data()));
				if (ok) {
					on_run(*first + static_cast<uint32_t>(g), *first + static_cast<uint32_t>(g), values);
				}
			}
			i += 2u;
			continue;
		}
		const std::optional<uint32_t> last = cid_value(second);
		if (!last.has_value() || i + 2u + group > array.size() || *last < *first) {
			i += 1u; // malformed: skipped
			continue;
		}
		if (*last > kMaxCid) {
			return oversized_range();
		}
		std::array<Held, 3> holders{};
		RECTO_PDF_TRY(const bool ok, read_group(array, i + 2u, holders.data()));
		if (ok) {
			on_run(*first, *last, values);
		}
		i += 2u + group;
	}
	return {};
}

template <typename V>
[[nodiscard]] V same_value(V value, uint64_t) noexcept
{
	return value;
}

template <typename V>
[[nodiscard]] std::vector<CidWidths::Run<V>> to_runs(const std::map<uint64_t, Span<V>>& set)
{
	std::vector<CidWidths::Run<V>> out;
	out.reserve(set.size());
	for (const auto& [key, span] : set) {
		out.push_back(CidWidths::Run<V>{static_cast<uint32_t>(span.low), static_cast<uint32_t>(span.high), span.value});
	}
	return out;
}

template <typename V>
[[nodiscard]] const CidWidths::Run<V>* find_run(const std::vector<CidWidths::Run<V>>& runs, uint32_t cid) noexcept
{
	auto it = std::upper_bound(runs.begin(), runs.end(), cid, [](uint32_t c, const CidWidths::Run<V>& r) { return c < r.low; });
	if (it == runs.begin()) {
		return nullptr;
	}
	--it;
	return cid <= it->high ? &*it : nullptr;
}

} // namespace

PdfResult<CidWidths> CidWidths::build(const PdfValue* dw, const PdfValue* w, const PdfValue* dw2, const PdfValue* w2,
	const IndirectObjectResolver* resolver, const SharedObjectResolver* shared) noexcept
{
	try {
		CidWidths widths;
		Held holder;
		RECTO_PDF_TRY(const PdfValue* dw_value, deref(dw, holder, resolver, shared));
		if (dw_value != nullptr) {
			if (const std::optional<double> n = dw_value->as_number()) {
				widths.default_width_ = *n / 1000.0;
			}
		}
		Held dw2_holder;
		RECTO_PDF_TRY(const PdfValue* dw2_value, deref(dw2, dw2_holder, resolver, shared));
		if (dw2_value != nullptr && dw2_value->is_array() && dw2_value->as_array()->size() == 2u) {
			const PdfArray& pair = *dw2_value->as_array();
			Held vy_holder;
			Held w1y_holder;
			RECTO_PDF_TRY(const PdfValue* vy_value, deref(&pair[0], vy_holder, resolver, shared));
			RECTO_PDF_TRY(const PdfValue* w1y_value, deref(&pair[1], w1y_holder, resolver, shared));
			const std::optional<double> vy = vy_value != nullptr ? vy_value->as_number() : std::nullopt;
			const std::optional<double> w1y = w1y_value != nullptr ? w1y_value->as_number() : std::nullopt;
			if (vy.has_value() && w1y.has_value()) {
				widths.default_vy_ = *vy / 1000.0;
				widths.default_w1y_ = *w1y / 1000.0;
			}
		}
		Held w_holder;
		RECTO_PDF_TRY(const PdfValue* w_value, deref(w, w_holder, resolver, shared));
		if (w_value != nullptr && w_value->is_array()) {
			std::map<uint64_t, Span<double>> set;
			RECTO_PDF_RETURN_IF_ERROR(walk_widths(*w_value->as_array(), 1u, resolver, shared,
				[&](uint32_t low, uint32_t high, const std::array<double, 3>& v) {
					lay_over(set, Span<double>{low, high, v[0]}, same_value<double>);
				}));
			widths.widths_ = to_runs(set);
		}
		Held w2_holder;
		RECTO_PDF_TRY(const PdfValue* w2_value, deref(w2, w2_holder, resolver, shared));
		if (w2_value != nullptr && w2_value->is_array()) {
			std::map<uint64_t, Span<CidVertical>> set;
			RECTO_PDF_RETURN_IF_ERROR(walk_widths(*w2_value->as_array(), 3u, resolver, shared,
				[&](uint32_t low, uint32_t high, const std::array<double, 3>& v) {
					lay_over(set, Span<CidVertical>{low, high, CidVertical{v[0], v[1], v[2]}}, same_value<CidVertical>);
				}));
			widths.verticals_ = to_runs(set);
		}
		return widths;
	} catch (const std::bad_alloc&) {
		return cid_error(ErrorCode::OutOfMemory, "Out of memory reading CIDFont widths");
	}
}

double CidWidths::advance(uint32_t cid) const noexcept
{
	const Run<double>* run = find_run(widths_, cid);
	return run != nullptr ? run->value : default_width_;
}

CidVertical CidWidths::vertical(uint32_t cid) const noexcept
{
	if (const Run<CidVertical>* run = find_run(verticals_, cid)) {
		return run->value;
	}
	return CidVertical{default_w1y_, advance(cid) / 2.0, default_vy_};
}

// ---------------------------------------------------------------------------------------------------------------------
// Type0Font and its assembly
// ---------------------------------------------------------------------------------------------------------------------

Type0Font::Type0Font(CidCMap cmap, CidFontKind kind, CidToGidMap map, CidWidths widths,
	std::shared_ptr<const LoadedFont> font) noexcept
	: cmap_(std::move(cmap))
	, kind_(kind)
	, map_(std::move(map))
	, widths_(std::move(widths))
	, font_(std::move(font))
{
}

Type0Glyph Type0Font::next(std::span<const uint8_t> bytes) const noexcept
{
	const CidCode code = cmap_.next(bytes);
	Type0Glyph out;
	out.code = code.code;
	out.length = code.length;
	out.cid = code.cid;
	if (code.length == 0u) {
		return out;
	}
	out.glyph = cid_glyph(code.cid, kind_, map_, font_.get());
	// A CID the font has no glyph for takes the code's notdef mapping, else CID 0 (9.7.6.3), for its glyph and metrics.
	if (out.glyph == 0u && code.cid != 0u && font_ != nullptr) {
		out.cid = code.notdef_cid;
		out.glyph = cid_glyph(out.cid, kind_, map_, font_.get());
	}
	out.advance = widths_.advance(out.cid);
	out.vertical = widths_.vertical(out.cid);
	return out;
}

namespace {

struct Type0Loader {
	const IndirectObjectResolver& resolver;
	const SharedObjectResolver* shared_resolver; // the document's cached values, held without a copy; null for copies
	std::set<uint32_t> cmap_objects; // the embedded CMaps on the `/UseCMap` chain so far

	// A value with its reference resolved, held by the caller; a reference that does not resolve is its error.
	[[nodiscard]] PdfResult<const PdfValue*> value(const PdfValue* v, Held& holder) const
	{
		if (v == nullptr || !v->is_ref()) {
			return v;
		}
		if (shared_resolver != nullptr && *shared_resolver) {
			RECTO_PDF_TRY(holder.shared, (*shared_resolver)(*v->as_ref()));
			return static_cast<const PdfValue*>(holder.shared.get());
		}
		RECTO_PDF_TRY(holder.owned, resolver(*v->as_ref()));
		return static_cast<const PdfValue*>(&holder.owned);
	}

	[[nodiscard]] PdfResult<std::vector<uint8_t>> bytes(const PdfStream& stream, size_t limit) const
	{
		return decode_stream(stream, limit, &resolver);
	}

	// An embedded CMap stream and the CMaps it inherits, `level` counting this one.
	[[nodiscard]] PdfResult<CidCMap> cmap(const PdfStream& stream, size_t level)
	{
		if (level > kMaxUseCMapLevels) {
			return cid_error(ErrorCode::ResourceLimit, "CID CMap inherits through more /UseCMap levels than its limit");
		}
		std::optional<CidCMap> base;
		if (const PdfValue* use = stream.dict.get("UseCMap")) {
			if (const std::optional<IndirectRef> ref = use->as_ref()) {
				if (!cmap_objects.insert(ref->obj_num).second) {
					return cid_error(ErrorCode::DamagedFormat, "CID CMap /UseCMap chain is a cycle");
				}
			}
			Held holder;
			RECTO_PDF_TRY(const PdfValue* inherited, value(use, holder));
			if (const std::optional<std::string_view> name = inherited->as_name()) {
				if (*name != "Identity-H" && *name != "Identity-V") {
					return cid_error(ErrorCode::UnsupportedFeature, "CID CMap inherits a CMap other than Identity");
				}
				base = CidCMap::identity(*name == "Identity-V");
			} else if (inherited->is_stream()) {
				RECTO_PDF_TRY_REF(parsed, cmap(*inherited->as_stream(), level + 1u));
				base = std::move(parsed);
			} else {
				return cid_error(ErrorCode::DamagedFormat, "CID CMap /UseCMap is neither a name nor a stream");
			}
		}
		RECTO_PDF_TRY(const std::vector<uint8_t> data, bytes(stream, kMaxCidCMapStreamBytes));
		RECTO_PDF_TRY_REF(parsed, CidCMap::parse(data, base.has_value() ? &*base : nullptr));
		Held mode_holder;
		RECTO_PDF_TRY(const PdfValue* mode, value(stream.dict.get("WMode"), mode_holder));
		if (mode != nullptr && mode->as_int().has_value()) {
			parsed.set_vertical(*mode->as_int() == 1);
		}
		return std::move(parsed);
	}

	// The program, and for an OpenType wrapper of a CID-keyed CFF the CID-to-glyph map its charset gives.
	struct Program {
		std::shared_ptr<const LoadedFont> font;
		std::optional<CidToGidMap> charset;
	};

	[[nodiscard]] PdfResult<Program> program_with_charset(const PdfDict& descriptor, CidFontKind kind) const
	{
		RECTO_PDF_TRY(std::shared_ptr<const LoadedFont> font, program(descriptor));
		Program out{std::move(font), std::nullopt};
		if (kind != CidFontKind::CIDFontType0 || out.font == nullptr || out.font->kind() != LoadedFontKind::OpenType) {
			return out;
		}
		const std::optional<std::span<const uint8_t>> table = opentype_cff_table(*out.font->program());
		if (!table.has_value()) {
			return out;
		}
		auto cff = load_charstring_font(
			std::make_shared<const std::vector<uint8_t>>(table->begin(), table->end()), CharstringFormat::CIDFontType0C);
		if (!cff.has_value()) {
			return cff.error();
		}
		if (!(*cff)->cid_keyed()) {
			return out; // a name-keyed CFF: the CID is the glyph
		}
		std::vector<uint16_t> glyphs(size_t{kMaxCid} + 1u, 0u);
		for (uint32_t cid = 0; cid <= kMaxCid; ++cid) {
			glyphs[cid] = (*cff)->glyph_for_cid(cid).value_or(0u);
		}
		out.charset = CidToGidMap::from_glyphs(std::move(glyphs));
		return out;
	}

	[[nodiscard]] PdfResult<std::shared_ptr<const LoadedFont>> program(const PdfDict& descriptor) const
	{
		for (const std::string_view key : {std::string_view("FontFile2"), std::string_view("FontFile3")}) {
			Held holder;
			RECTO_PDF_TRY(const PdfValue* file, value(descriptor.get(key), holder));
			if (file == nullptr) {
				continue;
			}
			if (!file->is_stream()) {
				return cid_error(ErrorCode::DamagedFormat, "CIDFont program is not a stream");
			}
			const PdfStream& stream = *file->as_stream();
			RECTO_PDF_TRY(std::vector<uint8_t> data, bytes(stream, kMaxDecodedStreamBytes));
			auto shared = std::make_shared<const std::vector<uint8_t>>(std::move(data));
			if (key == "FontFile2") {
				return load_embedded_font(std::move(shared), FontProgramFormat::TrueType);
			}
			Held subtype_holder;
			RECTO_PDF_TRY(const PdfValue* subtype_value, value(stream.dict.get("Subtype"), subtype_holder));
			const std::optional<std::string_view> subtype =
				subtype_value != nullptr ? subtype_value->as_name() : std::optional<std::string_view>{};
			if (subtype == "CIDFontType0C") {
				return load_charstring_font(std::move(shared), CharstringFormat::CIDFontType0C);
			}
			if (subtype == "Type1C") {
				return load_charstring_font(std::move(shared), CharstringFormat::Type1C);
			}
			if (subtype == "OpenType") {
				return load_embedded_font(std::move(shared), FontProgramFormat::OpenType);
			}
			return cid_error(ErrorCode::UnsupportedFeature, "CIDFont /FontFile3 has a subtype Recto does not read");
		}
		return std::shared_ptr<const LoadedFont>{};
	}
};

} // namespace

PdfResult<std::shared_ptr<const Type0Font>> load_type0_font(
	const PdfDict& type0, const IndirectObjectResolver& resolver, const SharedObjectResolver* shared,
	std::optional<PdfError>* program_error) noexcept
{
	try {
		Type0Loader loader{resolver, shared, {}};
		// The encoding CMap.
		Held encoding_holder;
		RECTO_PDF_TRY(const PdfValue* encoding, loader.value(type0.get("Encoding"), encoding_holder));
		std::optional<CidCMap> cmap;
		if (encoding != nullptr && encoding->is_name()) {
			const std::string_view name = *encoding->as_name();
			if (name != "Identity-H" && name != "Identity-V") {
				return cid_error(ErrorCode::UnsupportedFeature, "Type 0 font uses a predefined CMap other than Identity");
			}
			cmap = CidCMap::identity(name == "Identity-V");
		} else if (encoding != nullptr && encoding->is_stream()) {
			if (const std::optional<IndirectRef> ref = type0.get_ref("Encoding")) {
				loader.cmap_objects.insert(ref->obj_num);
			}
			RECTO_PDF_TRY_REF(parsed, loader.cmap(*encoding->as_stream(), 1u));
			cmap = std::move(parsed);
		} else {
			return cid_error(ErrorCode::DamagedFormat, "Type 0 font has no /Encoding CMap");
		}
		// The one descendant CIDFont.
		Held descendants_holder;
		RECTO_PDF_TRY(const PdfValue* descendants, loader.value(type0.get("DescendantFonts"), descendants_holder));
		if (descendants == nullptr || !descendants->is_array() || descendants->as_array()->size() != 1u) {
			return cid_error(ErrorCode::DamagedFormat, "Type 0 font does not have exactly one descendant font");
		}
		Held descendant_holder;
		RECTO_PDF_TRY(const PdfValue* descendant_value, loader.value(&(*descendants->as_array())[0], descendant_holder));
		if (descendant_value == nullptr || !descendant_value->is_dict()) {
			return cid_error(ErrorCode::DamagedFormat, "Type 0 font's descendant is not a font dictionary");
		}
		const PdfDict& descendant = *descendant_value->as_dict();
		Held subtype_holder;
		RECTO_PDF_TRY(const PdfValue* subtype_value, loader.value(descendant.get("Subtype"), subtype_holder));
		const std::optional<std::string_view> subtype =
			subtype_value != nullptr ? subtype_value->as_name() : std::optional<std::string_view>{};
		CidFontKind kind = CidFontKind::CIDFontType0;
		if (subtype == "CIDFontType2") {
			kind = CidFontKind::CIDFontType2;
		} else if (subtype != "CIDFontType0") {
			return cid_error(ErrorCode::DamagedFormat, "Type 0 font's descendant is neither CIDFontType0 nor CIDFontType2");
		}
		RECTO_PDF_TRY_REF(widths, CidWidths::build(descendant.get("DW"), descendant.get("W"), descendant.get("DW2"),
									  descendant.get("W2"), &resolver, shared));
		CidToGidMap map = CidToGidMap::identity();
		if (kind == CidFontKind::CIDFontType2) {
			Held map_holder;
			RECTO_PDF_TRY(const PdfValue* map_value, loader.value(descendant.get("CIDToGIDMap"), map_holder));
			if (map_value != nullptr && map_value->is_stream()) {
				RECTO_PDF_TRY(const std::vector<uint8_t> data, loader.bytes(*map_value->as_stream(), kMaxDecodedStreamBytes));
				RECTO_PDF_TRY_REF(parsed, CidToGidMap::parse(data));
				map = std::move(parsed);
			} else if (map_value != nullptr && map_value->as_name() != std::optional<std::string_view>("Identity")) {
				return cid_error(ErrorCode::DamagedFormat, "CIDToGIDMap is neither /Identity nor a stream");
			}
		}
		// The descriptor's program, if it embeds one.
		std::shared_ptr<const LoadedFont> font;
		Held descriptor_holder;
		RECTO_PDF_TRY(const PdfValue* descriptor, loader.value(descendant.get("FontDescriptor"), descriptor_holder));
		if (descriptor != nullptr && descriptor->is_dict()) {
			auto program = loader.program_with_charset(*descriptor->as_dict(), kind);
			const bool usable_without = program_error != nullptr && !program.has_value() &&
				(program.error().code == ErrorCode::DamagedFormat || program.error().code == ErrorCode::UnsupportedFeature ||
					program.error().code == ErrorCode::ResourceLimit);
			if (usable_without) {
				*program_error = program.error(); // the font keeps its CMap and widths, with no face
			} else {
				RECTO_PDF_RETURN_IF_ERROR(program);
				font = program->font;
				if (program->charset.has_value()) {
					map = std::move(*program->charset);
				}
			}
		}
		return std::shared_ptr<const Type0Font>(
			std::make_shared<const Type0Font>(std::move(*cmap), kind, std::move(map), std::move(widths), std::move(font)));
	} catch (const std::bad_alloc&) {
		return cid_error(ErrorCode::OutOfMemory, "Out of memory loading a Type 0 font");
	}
}

} // namespace recto::pdf
