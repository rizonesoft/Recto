// Simple-font encodings, glyph names to Unicode, TrueType glyph selection and `/ToUnicode` CMaps (ISO 32000-1
// sections 9.6.6, 9.10 and Annex D; D01 T02 section 28).
#include "font_encoding.h"

#include <algorithm>
#include <map>
#include <new>

namespace recto::pdf {

namespace {

struct AglEntry {
	std::string_view name;
	uint16_t first; // into kAglScalars
	uint8_t count;
};

struct ZapfEntry {
	std::string_view name;
	char32_t scalar;
};

#include "glyph_names.inc"

[[nodiscard]] const std::array<std::string_view, 256>& encoding_table(BaseEncoding encoding) noexcept
{
	switch (encoding) {
	case BaseEncoding::Standard:
		return kEncodingStandard;
	case BaseEncoding::MacRoman:
		return kEncodingMacRoman;
	case BaseEncoding::WinAnsi:
		return kEncodingWinAnsi;
	case BaseEncoding::MacExpert:
		return kEncodingMacExpert;
	case BaseEncoding::Symbol:
		return kEncodingSymbol;
	case BaseEncoding::ZapfDingbats:
		return kEncodingZapfDingbats;
	}
	return kEncodingStandard;
}

[[nodiscard]] bool is_upper_hex(char c) noexcept
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
}

[[nodiscard]] uint32_t hex_value(std::string_view digits) noexcept
{
	uint32_t value = 0;
	for (const char c : digits) {
		value = value * 16u + static_cast<uint32_t>(c <= '9' ? c - '0' : c - 'A' + 10);
	}
	return value;
}

[[nodiscard]] bool is_scalar(uint32_t value) noexcept
{
	return value <= 0x10FFFFu && (value < 0xD800u || value > 0xDFFFu);
}

// One `_`-separated component of a glyph name, by the AGL specification: appended to `out`, or nothing.
void append_component(std::string_view component, bool zapf_dingbats, std::u32string& out)
{
	// A ZapfDingbats font's names are looked up in zapfdingbats.txt first, then, like any font's, in the AGL.
	if (zapf_dingbats) {
		const auto it = std::lower_bound(std::begin(kZapfDingbatsEntries), std::end(kZapfDingbatsEntries), component,
			[](const ZapfEntry& entry, std::string_view key) { return entry.name < key; });
		if (it != std::end(kZapfDingbatsEntries) && it->name == component) {
			out.push_back(it->scalar);
			return;
		}
	}
	{
		const auto it = std::lower_bound(std::begin(kAglEntries), std::end(kAglEntries), component,
			[](const AglEntry& entry, std::string_view key) { return entry.name < key; });
		if (it != std::end(kAglEntries) && it->name == component) {
			out.append(&kAglScalars[it->first], it->count);
			return;
		}
	}
	// `uni` and groups of four upper-case hex digits; each a scalar in the BMP outside the surrogates.
	if (component.size() >= 7u && component.starts_with("uni") && (component.size() - 3u) % 4u == 0u &&
		std::all_of(component.begin() + 3, component.end(), is_upper_hex)) {
		std::u32string scalars;
		for (size_t at = 3u; at < component.size(); at += 4u) {
			const uint32_t value = hex_value(component.substr(at, 4u));
			if (!is_scalar(value)) {
				return;
			}
			scalars.push_back(static_cast<char32_t>(value));
		}
		out += scalars;
		return;
	}
	// `u` and four to six upper-case hex digits: one scalar.
	if (component.size() >= 5u && component.size() <= 7u && component.front() == 'u' &&
		std::all_of(component.begin() + 1, component.end(), is_upper_hex)) {
		const uint32_t value = hex_value(component.substr(1u));
		if (is_scalar(value)) {
			out.push_back(static_cast<char32_t>(value));
		}
	}
}

// The characters a Windows face draws for names the AGL maps to Adobe's private-use area (D01 T02 section 27): their
// Unicode characters.
constexpr std::array<std::pair<std::string_view, char32_t>, 1> kDrawableOverrides = {{{"commaaccent", U'̦'}}};

} // namespace

std::string_view base_encoding_name(BaseEncoding encoding, uint8_t code) noexcept
{
	return encoding_table(encoding)[code];
}

std::optional<BaseEncoding> base_encoding_from_name(std::string_view name) noexcept
{
	if (name == "WinAnsiEncoding") {
		return BaseEncoding::WinAnsi;
	}
	if (name == "MacRomanEncoding") {
		return BaseEncoding::MacRoman;
	}
	if (name == "MacExpertEncoding") {
		return BaseEncoding::MacExpert;
	}
	if (name == "StandardEncoding") {
		return BaseEncoding::Standard;
	}
	return std::nullopt;
}

std::optional<uint8_t> mac_os_roman_code(std::string_view name) noexcept
{
	if (name.empty()) {
		return std::nullopt;
	}
	for (size_t code = 0; code < kEncodingMacOSRoman.size(); ++code) {
		if (kEncodingMacOSRoman[code] == name) {
			return static_cast<uint8_t>(code);
		}
	}
	return std::nullopt;
}

PdfResult<std::u32string> glyph_name_unicode(std::string_view name, bool zapf_dingbats) noexcept
{
	try {
		std::u32string out;
		name = name.substr(0, name.find('.'));
		size_t at = 0;
		while (at <= name.size()) {
			const size_t end = std::min(name.find('_', at), name.size());
			append_component(name.substr(at, end - at), zapf_dingbats, out);
			at = end + 1u;
		}
		return out;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory reading a glyph name");
	}
}

std::optional<char32_t> drawable_unicode(std::string_view name, bool zapf_dingbats) noexcept
{
	for (const auto& [override_name, scalar] : kDrawableOverrides) {
		if (!zapf_dingbats && override_name == name) {
			return scalar;
		}
	}
	const PdfResult<std::u32string> unicode = glyph_name_unicode(name, zapf_dingbats);
	if (!unicode.has_value() || unicode->size() != 1u) {
		return std::nullopt;
	}
	const char32_t scalar = (*unicode)[0];
	if ((scalar >= 0xE000u && scalar <= 0xF8FFu) || scalar >= 0xF0000u) {
		return std::nullopt; // private use: no Windows face draws it as the glyph the name means
	}
	return scalar;
}

// ---------------------------------------------------------------------------------------------------------------------
// SimpleEncoding
// ---------------------------------------------------------------------------------------------------------------------

PdfResult<SimpleEncoding> SimpleEncoding::build(const PdfValue* encoding, bool symbolic, const GlyphNames* built_in) noexcept
{
	try {
		SimpleEncoding result;
		const auto fill_base = [&](BaseEncoding base) {
			const auto& table = encoding_table(base);
			for (size_t code = 0; code < 256u; ++code) {
				result.names_[code] = table[code];
			}
		};
		const auto fill_built_in = [&] {
			if (built_in != nullptr) {
				result.names_ = *built_in;
			} else if (!symbolic) {
				fill_base(BaseEncoding::Standard);
			}
		};
		const auto note_named = [&](BaseEncoding base) {
			if (base == BaseEncoding::MacRoman || base == BaseEncoding::WinAnsi) {
				result.named_base_ = base;
			}
		};
		const PdfDict* dict = encoding != nullptr ? encoding->as_dict() : nullptr;
		if (const std::optional<std::string_view> named = encoding != nullptr ? encoding->as_name() : std::nullopt) {
			if (const std::optional<BaseEncoding> base = base_encoding_from_name(*named)) {
				fill_base(*base);
				note_named(*base);
			} else {
				fill_built_in(); // an unknown name: as if absent
			}
		} else if (dict != nullptr) {
			const std::optional<std::string_view> base_name = dict->get_name("BaseEncoding");
			const std::optional<BaseEncoding> base = base_name.has_value() ? base_encoding_from_name(*base_name)
																		   : std::nullopt;
			if (base.has_value()) {
				fill_base(*base);
				note_named(*base);
			} else if (!symbolic) {
				fill_base(BaseEncoding::Standard); // Table 114: Differences apply to StandardEncoding
			} else if (built_in != nullptr) {
				result.names_ = *built_in; // and to the built-in encoding of a symbolic font
			}
			if (const PdfArray* differences = dict->get_array("Differences")) {
				result.has_differences_ = true;
				if (differences->size() > kMaxDifferencesEntries) {
					return make_pdf_error(ErrorCode::ResourceLimit, "/Differences array exceeds its entry limit");
				}
				int64_t code = -1;
				for (const PdfValue& entry : *differences) {
					if (const std::optional<int64_t> number = entry.as_int()) {
						code = *number >= 0 && *number <= 255 ? *number : -1;
					} else if (const std::optional<std::string_view> glyph = entry.as_name()) {
						if (code >= 0 && code <= 255) {
							if (glyph->size() <= kMaxGlyphNameBytes) {
								result.names_[static_cast<size_t>(code)] = std::string(*glyph);
							}
							++code;
						}
					}
				}
			}
		} else {
			fill_built_in();
		}
		return result;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory building a font encoding");
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// TrueType tables
// ---------------------------------------------------------------------------------------------------------------------

namespace {

[[nodiscard]] PdfError table_error(std::string_view message) noexcept
{
	return make_pdf_error(ErrorCode::DamagedFormat, message);
}

// Big-endian reads that fail rather than read past the span.
struct Reader {
	std::span<const uint8_t> bytes;

	[[nodiscard]] bool fits(size_t at, size_t count) const noexcept
	{
		return at <= bytes.size() && count <= bytes.size() - at;
	}
	[[nodiscard]] uint16_t u16(size_t at) const noexcept
	{
		return static_cast<uint16_t>((bytes[at] << 8u) | bytes[at + 1u]);
	}
	[[nodiscard]] uint32_t u32(size_t at) const noexcept
	{
		return (static_cast<uint32_t>(u16(at)) << 16u) | u16(at + 2u);
	}
};

// Adds `code` mapping to `glyph`, joining it to the previous group when both run on.
void add_single(std::vector<TrueTypeTables::Group>& groups, uint32_t code, uint32_t glyph)
{
	if (glyph == 0u) {
		return;
	}
	if (!groups.empty() && groups.back().end + 1u == code &&
		groups.back().glyph + (groups.back().end - groups.back().start) + 1u == glyph) {
		groups.back().end = code;
		return;
	}
	groups.push_back({code, code, glyph});
}

[[nodiscard]] PdfResult<std::optional<TrueTypeTables::Subtable>> parse_subtable(
	const Reader& cmap, size_t at, uint16_t platform, uint16_t encoding)
{
	TrueTypeTables::Subtable sub{platform, encoding, false, {}};
	if (!cmap.fits(at, 4u)) {
		return table_error("TrueType cmap subtable header is truncated");
	}
	const uint16_t format = cmap.u16(at);
	if (format == 12u) {
		if (!cmap.fits(at, 16u)) {
			return table_error("TrueType cmap format 12 header is truncated");
		}
		const uint32_t length = cmap.u32(at + 4u);
		const uint32_t count = cmap.u32(at + 12u);
		if (!cmap.fits(at, length) || length < 16u || count > (length - 16u) / 12u) {
			return table_error("TrueType cmap format 12 groups do not fit the subtable");
		}
		sub.groups.reserve(count);
		for (uint32_t i = 0; i < count; ++i) {
			const size_t g = at + 16u + size_t{i} * 12u;
			const uint32_t start = cmap.u32(g);
			const uint32_t end = cmap.u32(g + 4u);
			if (start > end || end > 0x10FFFFu || (!sub.groups.empty() && start <= sub.groups.back().end)) {
				return table_error("TrueType cmap format 12 group is reversed, out of range or overlapping");
			}
			sub.groups.push_back({start, end, cmap.u32(g + 8u)});
		}
		return std::optional<TrueTypeTables::Subtable>(std::move(sub));
	}
	const uint16_t length = cmap.u16(at + 2u);
	if (!cmap.fits(at, length) || length < 6u) {
		return table_error("TrueType cmap subtable does not fit the cmap table");
	}
	const Reader sub_reader{cmap.bytes.subspan(at, length)};
	if (format == 0u) {
		if (length < 262u) {
			return table_error("TrueType cmap format 0 is truncated");
		}
		for (uint32_t code = 0; code < 256u; ++code) {
			add_single(sub.groups, code, sub_reader.bytes[6u + code]);
		}
	} else if (format == 6u) {
		if (length < 10u) {
			return table_error("TrueType cmap format 6 is truncated");
		}
		const uint32_t first = sub_reader.u16(6u);
		const uint32_t count = sub_reader.u16(8u);
		if (!sub_reader.fits(10u, size_t{count} * 2u) || first + count > 0x10000u) {
			return table_error("TrueType cmap format 6 glyph array does not fit");
		}
		for (uint32_t i = 0; i < count; ++i) {
			add_single(sub.groups, first + i, sub_reader.u16(10u + size_t{i} * 2u));
		}
	} else if (format == 4u) {
		if (length < 14u) {
			return table_error("TrueType cmap format 4 header is truncated");
		}
		const uint16_t seg_x2 = sub_reader.u16(6u);
		const size_t segments = seg_x2 / 2u;
		const size_t ends = 14u;
		const size_t starts = ends + seg_x2 + 2u;
		const size_t deltas = starts + seg_x2;
		const size_t offsets = deltas + seg_x2;
		if (seg_x2 % 2u != 0u || segments == 0u || !sub_reader.fits(offsets, seg_x2)) {
			return table_error("TrueType cmap format 4 segment arrays do not fit the subtable");
		}
		sub.wraps16 = true;
		uint32_t previous_end = 0;
		for (size_t i = 0; i < segments; ++i) {
			const uint32_t end = sub_reader.u16(ends + i * 2u);
			const uint32_t start = sub_reader.u16(starts + i * 2u);
			const uint16_t delta = sub_reader.u16(deltas + i * 2u);
			const uint16_t range_offset = sub_reader.u16(offsets + i * 2u);
			if (start > end || (i > 0u && start <= previous_end)) {
				return table_error("TrueType cmap format 4 segment is reversed or overlapping");
			}
			previous_end = end;
			if (start == 0xFFFFu) {
				continue; // the closing segment
			}
			if (range_offset == 0u) {
				sub.groups.push_back({start, end, (start + delta) & 0xFFFFu});
				continue;
			}
			for (uint32_t code = start; code <= end; ++code) {
				const size_t index_at = offsets + i * 2u + range_offset + size_t{code - start} * 2u;
				if (!sub_reader.fits(index_at, 2u)) {
					return table_error("TrueType cmap format 4 idRangeOffset points outside the glyph array");
				}
				const uint16_t glyph = sub_reader.u16(index_at);
				add_single(sub.groups, code, glyph == 0u ? 0u : (glyph + delta) & 0xFFFFu);
			}
		}
	} else {
		return std::optional<TrueTypeTables::Subtable>{}; // a format Recto does not read
	}
	return std::optional<TrueTypeTables::Subtable>(std::move(sub));
}

} // namespace

PdfResult<TrueTypeTables> TrueTypeTables::parse(std::span<const uint8_t> program) noexcept
{
	try {
		TrueTypeTables tables;
		const Reader font{program};
		size_t directory = 0;
		if (font.fits(0u, 12u) && font.u32(0u) == 0x74746366u) { // 'ttcf': the first font of a collection
			if (!font.fits(12u, 4u)) {
				return table_error("TrueType collection header is truncated");
			}
			directory = font.u32(12u);
		}
		if (!font.fits(directory, 12u)) {
			return table_error("TrueType table directory is truncated");
		}
		const uint16_t table_count = font.u16(directory + 4u);
		if (!font.fits(directory + 12u, size_t{table_count} * 16u)) {
			return table_error("TrueType table records do not fit the font program");
		}
		std::optional<Reader> cmap;
		std::optional<Reader> post;
		for (size_t i = 0; i < table_count; ++i) {
			const size_t record = directory + 12u + i * 16u;
			const uint32_t tag = font.u32(record);
			const uint32_t offset = font.u32(record + 8u);
			const uint32_t length = font.u32(record + 12u);
			if (tag != 0x636D6170u && tag != 0x706F7374u) {
				continue;
			}
			if (!font.fits(offset, length)) {
				return table_error("TrueType cmap or post table lies outside the font program");
			}
			(tag == 0x636D6170u ? cmap : post) = Reader{program.subspan(offset, length)};
		}
		if (cmap.has_value()) {
			if (!cmap->fits(0u, 4u)) {
				return table_error("TrueType cmap header is truncated");
			}
			const uint16_t count = cmap->u16(2u);
			if (!cmap->fits(4u, size_t{count} * 8u)) {
				return table_error("TrueType cmap encoding records do not fit the table");
			}
			for (size_t i = 0; i < count; ++i) {
				const uint16_t platform = cmap->u16(4u + i * 8u);
				const uint16_t encoding = cmap->u16(4u + i * 8u + 2u);
				const bool wanted = (platform == 3u && (encoding == 0u || encoding == 1u || encoding == 10u)) ||
					(platform == 1u && encoding == 0u);
				if (!wanted || tables.has(platform, encoding)) {
					continue;
				}
				RECTO_PDF_TRY(std::optional<Subtable> sub,
					parse_subtable(*cmap, cmap->u32(4u + i * 8u + 4u), platform, encoding));
				if (sub.has_value()) {
					tables.subtables_.push_back(std::move(*sub));
				}
			}
		}
		if (post.has_value() && post->fits(0u, 32u)) {
			const uint32_t version = post->u32(0u);
			if (version == 0x00010000u) {
				for (uint16_t glyph = 0; glyph < kMacGlyphNames.size(); ++glyph) {
					tables.post_names_.emplace_back(std::string(kMacGlyphNames[glyph]), glyph);
				}
			} else if (version == 0x00020000u) {
				if (!post->fits(32u, 2u)) {
					return table_error("TrueType post format 2 header is truncated");
				}
				const uint16_t glyphs = post->u16(32u);
				const size_t strings_at = 34u + size_t{glyphs} * 2u;
				if (!post->fits(34u, size_t{glyphs} * 2u)) {
					return table_error("TrueType post format 2 glyph name indices do not fit the table");
				}
				std::vector<std::string_view> strings;
				for (size_t at = strings_at; at < post->bytes.size();) {
					const size_t length = post->bytes[at];
					if (!post->fits(at + 1u, length)) {
						return table_error("TrueType post format 2 glyph name runs past the table");
					}
					strings.emplace_back(reinterpret_cast<const char*>(post->bytes.data() + at + 1u), length);
					at += 1u + length;
				}
				for (uint16_t glyph = 0; glyph < glyphs; ++glyph) {
					const uint16_t index = post->u16(34u + size_t{glyph} * 2u);
					std::string_view name;
					if (index < kMacGlyphNames.size()) {
						name = kMacGlyphNames[index];
					} else if (size_t{index} - kMacGlyphNames.size() < strings.size()) {
						name = strings[index - kMacGlyphNames.size()];
					}
					if (!name.empty()) {
						tables.post_names_.emplace_back(std::string(name), glyph);
					}
				}
			}
			std::stable_sort(tables.post_names_.begin(), tables.post_names_.end(),
				[](const auto& a, const auto& b) { return a.first < b.first; });
			tables.post_names_.erase(std::unique(tables.post_names_.begin(), tables.post_names_.end(),
										 [](const auto& a, const auto& b) { return a.first == b.first; }),
				tables.post_names_.end()); // the lowest glyph of a repeated name stays
		}
		// The Unicode subtable read backwards, the lowest code of each glyph winning: each code visited once.
		const Subtable* unicode = nullptr;
		for (const uint16_t encoding : {uint16_t{10}, uint16_t{1}}) {
			for (const Subtable& sub : tables.subtables_) {
				if (unicode == nullptr && sub.platform == 3u && sub.encoding == encoding) {
					unicode = &sub;
				}
			}
		}
		if (unicode != nullptr) {
			std::vector<char32_t> first(0x10000u, U'\0');
			for (const Group& group : unicode->groups) {
				for (uint32_t code = group.start; code <= group.end; ++code) {
					uint32_t glyph = group.glyph + (code - group.start);
					if (unicode->wraps16) {
						glyph &= 0xFFFFu;
					}
					if (glyph != 0u && glyph < first.size() && first[glyph] == U'\0' && is_scalar(code)) {
						first[glyph] = static_cast<char32_t>(code);
					}
				}
			}
			for (uint32_t glyph = 1; glyph < first.size(); ++glyph) {
				if (first[glyph] != U'\0') {
					tables.unicode_of_glyph_.emplace_back(static_cast<uint16_t>(glyph), first[glyph]);
				}
			}
		}
		return tables;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory reading a TrueType font's cmap");
	}
}

bool TrueTypeTables::has(uint16_t platform, uint16_t encoding) const noexcept
{
	return std::any_of(subtables_.begin(), subtables_.end(),
		[&](const Subtable& sub) { return sub.platform == platform && sub.encoding == encoding; });
}

std::optional<uint16_t> TrueTypeTables::lookup(uint16_t platform, uint16_t encoding, uint32_t code) const noexcept
{
	for (const Subtable& sub : subtables_) {
		if (sub.platform != platform || sub.encoding != encoding) {
			continue;
		}
		const auto it = std::upper_bound(sub.groups.begin(), sub.groups.end(), code,
			[](uint32_t key, const Group& group) { return key < group.start; });
		if (it == sub.groups.begin()) {
			return std::nullopt;
		}
		const Group& group = *(it - 1);
		if (code > group.end) {
			return std::nullopt;
		}
		uint32_t glyph = group.glyph + (code - group.start);
		if (sub.wraps16) {
			glyph &= 0xFFFFu;
		}
		if (glyph == 0u || glyph > 0xFFFFu) {
			return std::nullopt;
		}
		return static_cast<uint16_t>(glyph);
	}
	return std::nullopt;
}

std::optional<uint16_t> TrueTypeTables::glyph_for_name(std::string_view name) const noexcept
{
	const auto it = std::lower_bound(post_names_.begin(), post_names_.end(), name,
		[](const auto& entry, std::string_view key) { return entry.first < key; });
	if (it == post_names_.end() || it->first != name) {
		return std::nullopt;
	}
	return it->second;
}

std::optional<char32_t> TrueTypeTables::unicode_for_glyph(uint16_t glyph) const noexcept
{
	const auto it = std::lower_bound(unicode_of_glyph_.begin(), unicode_of_glyph_.end(), glyph,
		[](const auto& entry, uint16_t key) { return entry.first < key; });
	if (it == unicode_of_glyph_.end() || it->first != glyph) {
		return std::nullopt;
	}
	return it->second;
}

uint16_t simple_truetype_glyph(const TrueTypeTables& tables, const SimpleEncoding& encoding, bool symbolic,
	bool nonsymbolic, uint8_t code) noexcept
{
	const bool name_path = encoding.named_base().has_value() || encoding.has_differences() || (nonsymbolic && !symbolic);
	if (name_path) {
		std::string_view name = encoding.name(code);
		if (name.empty()) {
			name = base_encoding_name(BaseEncoding::Standard, code); // undefined entries come from StandardEncoding
		}
		if (!name.empty()) {
			for (const uint16_t unicode_encoding : {uint16_t{10}, uint16_t{1}}) {
				if (!tables.has(3u, unicode_encoding)) {
					continue;
				}
				const PdfResult<std::u32string> unicode = glyph_name_unicode(name);
				if (unicode.has_value() && unicode->size() == 1u) {
					if (const auto glyph = tables.lookup(3u, unicode_encoding, (*unicode)[0])) {
						return *glyph;
					}
				}
			}
			if (const std::optional<uint8_t> mac = mac_os_roman_code(name); mac.has_value() && tables.has(1u, 0u)) {
				if (const auto glyph = tables.lookup(1u, 0u, *mac)) {
					return *glyph;
				}
			}
			if (const auto glyph = tables.glyph_for_name(name)) {
				return *glyph;
			}
		}
	}
	if (tables.has(3u, 0u)) {
		for (const uint32_t base : {0u, 0xF000u, 0xF100u, 0xF200u}) {
			if (const auto glyph = tables.lookup(3u, 0u, base + code)) {
				return *glyph;
			}
		}
	}
	if (tables.has(1u, 0u)) {
		if (const auto glyph = tables.lookup(1u, 0u, code)) {
			return *glyph;
		}
	}
	return code;
}

// ---------------------------------------------------------------------------------------------------------------------
// ToUnicode CMaps
// ---------------------------------------------------------------------------------------------------------------------

namespace {

struct ToUnicodeParser {
	enum class Kind : uint8_t { End, Hex, Name, Keyword, ArrayOpen, ArrayClose, Other };
	struct Token {
		Kind kind = Kind::End;
		std::string_view text{};
		std::string hex{};
	};

	std::span<const uint8_t> in;
	size_t at = 0;

	[[nodiscard]] static bool white(uint8_t c) noexcept
	{
		return c == 0u || c == 9u || c == 10u || c == 12u || c == 13u || c == 32u;
	}
	[[nodiscard]] static bool delimiter(uint8_t c) noexcept
	{
		return white(c) || c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' ||
			c == '}' || c == '/' || c == '%';
	}

	// The next token; a hex string's bytes are decoded, past `kMaxToUnicodeTargetBytes` it is `DamagedFormat`.
	[[nodiscard]] PdfResult<Token> next()
	{
		for (;;) {
			while (at < in.size() && white(in[at])) {
				++at;
			}
			if (at < in.size() && in[at] == '%') {
				while (at < in.size() && in[at] != '\n' && in[at] != '\r') {
					++at;
				}
				continue;
			}
			break;
		}
		Token token;
		if (at >= in.size()) {
			return token;
		}
		const uint8_t c = in[at];
		if (c == '<' && at + 1u < in.size() && in[at + 1u] == '<') {
			at += 2u;
			token.kind = Kind::Other;
			return token;
		}
		if (c == '<') {
			++at;
			int high = -1;
			token.kind = Kind::Hex;
			while (at < in.size() && in[at] != '>') {
				const uint8_t d = in[at++];
				if (white(d)) {
					continue;
				}
				int value = -1;
				if (d >= '0' && d <= '9') {
					value = d - '0';
				} else if (d >= 'a' && d <= 'f') {
					value = d - 'a' + 10;
				} else if (d >= 'A' && d <= 'F') {
					value = d - 'A' + 10;
				} else {
					return make_pdf_error(ErrorCode::DamagedFormat, "ToUnicode CMap hex string holds a non-hex byte");
				}
				if (high < 0) {
					high = value;
				} else {
					token.hex.push_back(static_cast<char>((high << 4) | value));
					high = -1;
					if (token.hex.size() > kMaxToUnicodeTargetBytes) {
						return make_pdf_error(ErrorCode::DamagedFormat, "ToUnicode CMap hex string is too long");
					}
				}
			}
			if (at >= in.size()) {
				return make_pdf_error(ErrorCode::DamagedFormat, "ToUnicode CMap hex string is not closed");
			}
			++at;
			if (high >= 0) {
				token.hex.push_back(static_cast<char>(high << 4)); // an odd digit count: the last digit padded
			}
			return token;
		}
		if (c == '>' && at + 1u < in.size() && in[at + 1u] == '>') {
			at += 2u;
			token.kind = Kind::Other;
			return token;
		}
		if (c == '[' || c == ']') {
			++at;
			token.kind = c == '[' ? Kind::ArrayOpen : Kind::ArrayClose;
			return token;
		}
		if (c == '(') {
			// A literal string, skipped with its nesting and escapes.
			int depth = 0;
			do {
				if (in[at] == '\\') {
					++at;
				} else if (in[at] == '(') {
					++depth;
				} else if (in[at] == ')') {
					--depth;
				}
				++at;
			} while (at < in.size() && depth > 0);
			token.kind = Kind::Other;
			return token;
		}
		const size_t start = c == '/' ? at + 1u : at;
		at = start;
		while (at < in.size() && !delimiter(in[at])) {
			++at;
		}
		if (at == start && c != '/') {
			++at; // a stray delimiter ('>', ')', '{', '}')
			token.kind = Kind::Other;
			return token;
		}
		token.kind = c == '/' ? Kind::Name : Kind::Keyword;
		token.text = std::string_view(reinterpret_cast<const char*>(in.data() + start), at - start);
		return token;
	}
};

[[nodiscard]] uint32_t code_of(std::string_view bytes) noexcept
{
	uint32_t code = 0;
	for (const char b : bytes) {
		code = (code << 8u) | static_cast<uint8_t>(b);
	}
	return code;
}

// UTF-16BE to scalars; nothing for an odd length or an unpaired surrogate.
[[nodiscard]] std::optional<std::u32string> utf16_target(std::string_view bytes)
{
	if (bytes.empty() || bytes.size() % 2u != 0u) {
		return std::nullopt;
	}
	std::u32string out;
	for (size_t i = 0; i < bytes.size(); i += 2u) {
		const uint32_t unit = (static_cast<uint32_t>(static_cast<uint8_t>(bytes[i])) << 8u) | static_cast<uint8_t>(bytes[i + 1u]);
		if (unit >= 0xD800u && unit <= 0xDBFFu) {
			if (i + 3u >= bytes.size()) {
				return std::nullopt;
			}
			const uint32_t low = (static_cast<uint32_t>(static_cast<uint8_t>(bytes[i + 2u])) << 8u) |
				static_cast<uint8_t>(bytes[i + 3u]);
			if (low < 0xDC00u || low > 0xDFFFu) {
				return std::nullopt;
			}
			out.push_back(static_cast<char32_t>(0x10000u + ((unit - 0xD800u) << 10u) + (low - 0xDC00u)));
			i += 2u;
		} else if (unit >= 0xDC00u && unit <= 0xDFFFu) {
			return std::nullopt;
		} else {
			out.push_back(static_cast<char32_t>(unit));
		}
	}
	return out;
}

} // namespace

PdfResult<ToUnicodeCMap> ToUnicodeCMap::parse(std::span<const uint8_t> stream) noexcept
{
	try {
		if (stream.size() > kMaxToUnicodeStreamBytes) {
			return make_pdf_error(ErrorCode::ResourceLimit, "ToUnicode CMap stream exceeds its size limit");
		}
		ToUnicodeParser parser{stream};
		ToUnicodeCMap cmap;
		// Mappings in the order the CMap gives them; a later one wins where they overlap.
		std::vector<Mapping> given;
		size_t mapped_codes = 0;
		const auto add = [&](uint32_t low, uint32_t high, uint8_t length, std::u32string target) -> PdfResult<void> {
			const size_t span = size_t{high} - low + 1u;
			if (span > kMaxToUnicodeCodes - mapped_codes) {
				return make_pdf_error(ErrorCode::ResourceLimit, "ToUnicode CMap maps more codes than its limit");
			}
			mapped_codes += span;
			given.push_back(Mapping{low, high, length, std::move(target)});
			return {};
		};
		const auto source = [](const ToUnicodeParser::Token& token) -> std::optional<std::pair<uint32_t, uint8_t>> {
			if (token.kind != ToUnicodeParser::Kind::Hex || token.hex.empty() || token.hex.size() > 4u) {
				return std::nullopt;
			}
			return std::pair{code_of(token.hex), static_cast<uint8_t>(token.hex.size())};
		};
		const auto malformed = [](std::string_view what) {
			return make_pdf_error(ErrorCode::DamagedFormat, what);
		};
		for (;;) {
			RECTO_PDF_TRY(ToUnicodeParser::Token token, parser.next());
			if (token.kind == ToUnicodeParser::Kind::End) {
				break;
			}
			if (token.kind != ToUnicodeParser::Kind::Keyword) {
				continue;
			}
			if (token.text == "usecmap") {
				return make_pdf_error(ErrorCode::UnsupportedFeature, "ToUnicode CMap usecmap is not supported");
			}
			if (token.text == "begincodespacerange") {
				for (;;) {
					RECTO_PDF_TRY(ToUnicodeParser::Token low, parser.next());
					if (low.kind == ToUnicodeParser::Kind::Keyword && low.text == "endcodespacerange") {
						break;
					}
					RECTO_PDF_TRY(ToUnicodeParser::Token high, parser.next());
					const auto lo = source(low);
					const auto hi = source(high);
					if (!lo.has_value() || !hi.has_value() || lo->second != hi->second || lo->first > hi->first) {
						return malformed("ToUnicode CMap codespace range is malformed");
					}
					if (cmap.codespace_.size() >= kMaxCodespaceRanges) {
						return make_pdf_error(ErrorCode::ResourceLimit, "ToUnicode CMap has too many codespace ranges");
					}
					cmap.codespace_.push_back(Range{lo->first, hi->first, lo->second});
				}
			} else if (token.text == "beginbfchar") {
				for (;;) {
					RECTO_PDF_TRY(ToUnicodeParser::Token src, parser.next());
					if (src.kind == ToUnicodeParser::Kind::Keyword && src.text == "endbfchar") {
						break;
					}
					RECTO_PDF_TRY(ToUnicodeParser::Token dst, parser.next());
					const auto code = source(src);
					if (!code.has_value() || (dst.kind != ToUnicodeParser::Kind::Hex && dst.kind != ToUnicodeParser::Kind::Name)) {
						return malformed("ToUnicode CMap bfchar entry is malformed");
					}
					std::optional<std::u32string> target;
					if (dst.kind == ToUnicodeParser::Kind::Hex) {
						target = utf16_target(dst.hex);
					} else if (PdfResult<std::u32string> named = glyph_name_unicode(dst.text);
							   named.has_value() && !named->empty()) {
						target = std::move(*named);
					}
					if (target.has_value()) {
						RECTO_PDF_RETURN_IF_ERROR(add(code->first, code->first, code->second, std::move(*target)));
					}
				}
			} else if (token.text == "beginbfrange") {
				for (;;) {
					RECTO_PDF_TRY(ToUnicodeParser::Token low, parser.next());
					if (low.kind == ToUnicodeParser::Kind::Keyword && low.text == "endbfrange") {
						break;
					}
					RECTO_PDF_TRY(ToUnicodeParser::Token high, parser.next());
					RECTO_PDF_TRY(ToUnicodeParser::Token dst, parser.next());
					const auto lo = source(low);
					const auto hi = source(high);
					if (!lo.has_value() || !hi.has_value() || lo->second != hi->second || lo->first > hi->first) {
						return malformed("ToUnicode CMap bfrange bounds are malformed");
					}
					if (hi->first - lo->first >= kMaxToUnicodeCodes) {
						return make_pdf_error(ErrorCode::ResourceLimit, "ToUnicode CMap bfrange spans more codes than its limit");
					}
					if (dst.kind == ToUnicodeParser::Kind::Hex) {
						if (std::optional<std::u32string> target = utf16_target(dst.hex)) {
							if (size_t{target->back()} + (hi->first - lo->first) <= 0x10FFFFu) {
								RECTO_PDF_RETURN_IF_ERROR(add(lo->first, hi->first, lo->second, std::move(*target)));
							}
						}
					} else if (dst.kind == ToUnicodeParser::Kind::ArrayOpen) {
						uint32_t code = lo->first;
						for (;;) {
							RECTO_PDF_TRY(ToUnicodeParser::Token element, parser.next());
							if (element.kind == ToUnicodeParser::Kind::ArrayClose) {
								break;
							}
							if (element.kind != ToUnicodeParser::Kind::Hex) {
								return malformed("ToUnicode CMap bfrange array holds a non-string");
							}
							if (code <= hi->first) {
								if (std::optional<std::u32string> target = utf16_target(element.hex)) {
									RECTO_PDF_RETURN_IF_ERROR(add(code, code, lo->second, std::move(*target)));
								}
							}
							++code;
						}
					} else {
						return malformed("ToUnicode CMap bfrange target is neither a string nor an array");
					}
				}
			}
		}
		// Later mappings win: placed from the last given to the first, each keeping only what nothing later covered.
		std::map<std::pair<uint8_t, uint32_t>, Mapping> placed;
		for (auto it = given.rbegin(); it != given.rend(); ++it) {
			uint64_t from = it->low;
			const uint64_t last = it->high;
			while (from <= last) {
				const auto key = std::pair{it->length, static_cast<uint32_t>(from)};
				auto next = placed.lower_bound(key);
				// A placed mapping that starts at or before `from` and reaches it covers it already.
				if (next != placed.end() && next->first.first == it->length && next->first.second == from) {
					from = uint64_t{next->second.high} + 1u;
					continue;
				}
				if (next != placed.begin()) {
					const auto previous = std::prev(next);
					if (previous->first.first == it->length && previous->second.high >= from) {
						from = uint64_t{previous->second.high} + 1u;
						continue;
					}
				}
				uint64_t to = last;
				if (next != placed.end() && next->first.first == it->length && next->first.second <= to) {
					to = uint64_t{next->first.second} - 1u;
				}
				std::u32string target = it->target;
				target.back() = static_cast<char32_t>(target.back() + (from - it->low));
				placed.emplace(key, Mapping{static_cast<uint32_t>(from), static_cast<uint32_t>(to), it->length,
										std::move(target)});
				from = to + 1u;
			}
		}
		cmap.mappings_.reserve(placed.size());
		for (auto& [key, mapping] : placed) {
			cmap.mappings_.push_back(std::move(mapping));
		}
		return cmap;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory reading a ToUnicode CMap");
	}
}

std::optional<std::u32string> ToUnicodeCMap::lookup(uint32_t code, uint8_t length) const noexcept
{
	const auto it = std::upper_bound(mappings_.begin(), mappings_.end(), std::pair{length, code},
		[](const std::pair<uint8_t, uint32_t>& key, const Mapping& m) {
			return key.first < m.length || (key.first == m.length && key.second < m.low);
		});
	if (it == mappings_.begin()) {
		return std::nullopt;
	}
	const Mapping& m = *(it - 1);
	if (m.length != length || code > m.high) {
		return std::nullopt;
	}
	try {
		std::u32string target = m.target;
		target.back() = static_cast<char32_t>(target.back() + (code - m.low));
		return target;
	} catch (const std::bad_alloc&) {
		return std::nullopt;
	}
}

uint8_t ToUnicodeCMap::code_length(std::span<const uint8_t> bytes) const noexcept
{
	if (codespace_.empty()) {
		return 1u;
	}
	uint8_t shortest = 4u;
	for (uint8_t length = 1; length <= 4u; ++length) {
		for (const Range& range : codespace_) {
			if (range.length != length) {
				continue;
			}
			shortest = std::min(shortest, length);
			if (bytes.size() < length) {
				continue;
			}
			// Each byte within the range's byte at that place (Adobe Technical Note 5014, codespace ranges).
			bool inside = true;
			for (uint8_t i = 0; i < length && inside; ++i) {
				const uint32_t shift = 8u * (length - 1u - i);
				const uint32_t b = bytes[i];
				inside = b >= ((range.low >> shift) & 0xFFu) && b <= ((range.high >> shift) & 0xFFu);
			}
			if (inside) {
				return length;
			}
		}
	}
	return shortest;
}

PdfResult<std::u32string> simple_font_unicode(const SimpleFontText& font, uint8_t code) noexcept
{
	if (font.to_unicode != nullptr) {
		if (std::optional<std::u32string> mapped = font.to_unicode->lookup(code, 1u); mapped.has_value() && !mapped->empty()) {
			return std::move(*mapped);
		}
	}
	if (font.encoding != nullptr) {
		if (const std::string_view name = font.encoding->name(code); !name.empty()) {
			RECTO_PDF_TRY(std::u32string unicode, glyph_name_unicode(name, font.zapf_dingbats));
			if (!unicode.empty()) {
				return unicode;
			}
		}
		if (font.truetype != nullptr) {
			const uint16_t glyph = simple_truetype_glyph(*font.truetype, *font.encoding, font.symbolic, font.nonsymbolic, code);
			if (const std::optional<char32_t> scalar = glyph != 0u ? font.truetype->unicode_for_glyph(glyph) : std::nullopt) {
				try {
					return std::u32string(1u, *scalar);
				} catch (const std::bad_alloc&) {
					return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory mapping a code to Unicode");
				}
			}
		}
	}
	return std::u32string{};
}

} // namespace recto::pdf
