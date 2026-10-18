// In-memory PDF object representation (ISO 32000-1:2008 section 7.3,
// ISO 32000-2:2020 section 7.3) for recto-pdf (D01 T01 section 2).
#include "object.h"

#include <array>
#include <cmath>
#include <new>

namespace recto::pdf {
namespace {

// ISO 32000-1:2008 Annex D, Table D.2 (PDFDocEncoding).
// Maps byte values 0x00..0xFF to Unicode code points (0xFFFD = undefined).
// Note: 0x00..0x17 and 0x7F are undefined in PDFDocEncoding except standard
// control characters (\t 0x09, \n 0x0A, \r 0x0D) which we preserve as-is.
constexpr std::array<uint16_t, 256> kPdfDocEncodingToUnicode = {
	// 0x00..0x0F
	0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
	0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
	// 0x10..0x1F (ISO 32000-1 Table D.2 maps 0x18..0x1F to diacritics)
	0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
	0x02D8, // 0x18 BREVE
	0x02C7, // 0x19 CARON
	0x02C6, // 0x1A MODIFIER LETTER CIRCUMFLEX ACCENT
	0x02D9, // 0x1B DOT ABOVE
	0x02DD, // 0x1C DOUBLE ACUTE ACCENT
	0x02DB, // 0x1D OGONEK
	0x02DA, // 0x1E RING ABOVE
	0x02DC, // 0x1F SMALL TILDE
	// 0x20..0x7F (ASCII, 0x7F undefined -> 0xFFFD)
	0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
	0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
	0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
	0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
	0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
	0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
	0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
	0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
	0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
	0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
	0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
	0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0xFFFD,
	// 0x80..0x8F
	0x2022, // 0x80 BULLET
	0x2020, // 0x81 DAGGER
	0x2021, // 0x82 DOUBLE DAGGER
	0x2026, // 0x83 HORIZONTAL ELLIPSIS
	0x2014, // 0x84 EM DASH
	0x2013, // 0x85 EN DASH
	0x0192, // 0x86 LATIN SMALL LETTER F WITH HOOK
	0x2044, // 0x87 FRACTION SLASH
	0x2039, // 0x88 SINGLE LEFT-POINTING ANGLE QUOTATION MARK
	0x203A, // 0x89 SINGLE RIGHT-POINTING ANGLE QUOTATION MARK
	0x2212, // 0x8A MINUS SIGN
	0x2030, // 0x8B PER MILLE SIGN
	0x201E, // 0x8C DOUBLE LOW-9 QUOTATION MARK
	0x201C, // 0x8D LEFT DOUBLE QUOTATION MARK
	0x201D, // 0x8E RIGHT DOUBLE QUOTATION MARK
	0x2018, // 0x8F LEFT SINGLE QUOTATION MARK
	// 0x90..0x9F
	0x2019, // 0x90 RIGHT SINGLE QUOTATION MARK
	0x201A, // 0x91 SINGLE LOW-9 QUOTATION MARK
	0x2122, // 0x92 TRADE MARK SIGN
	0xFB01, // 0x93 LATIN SMALL LIGATURE FI
	0xFB02, // 0x94 LATIN SMALL LIGATURE FL
	0x0141, // 0x95 LATIN CAPITAL LETTER L WITH STROKE
	0x0152, // 0x96 LATIN CAPITAL LIGATURE OE
	0x0160, // 0x97 LATIN CAPITAL LETTER S WITH CARON
	0x0178, // 0x98 LATIN CAPITAL LETTER Y WITH DIAERESIS
	0x017D, // 0x99 LATIN CAPITAL LETTER Z WITH CARON
	0x0131, // 0x9A LATIN SMALL LETTER DOTLESS I
	0x0142, // 0x9B LATIN SMALL LETTER L WITH STROKE
	0x0153, // 0x9C LATIN SMALL LIGATURE OE
	0x0161, // 0x9D LATIN SMALL LETTER S WITH CARON
	0x017E, // 0x9E LATIN SMALL LETTER Z WITH CARON
	0xFFFD, // 0x9F undefined
	// 0xA0..0xAF
	0x20AC, // 0xA0 EURO SIGN
	0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
	0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0xFFFD, 0x00AE, 0x00AF,
	// 0xB0..0xFF (identical to ISO-8859-1 / Unicode Latin-1 Supplement)
	0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
	0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
	0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
	0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
	0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,
	0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,
	0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,
	0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
	0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7,
	0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF,
};

void append_utf8_codepoint(std::string& out, uint32_t cp)
{
	if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
		cp = 0xFFFDu;
	}
	if (cp <= 0x7Fu) {
		out.push_back(static_cast<char>(cp));
	} else if (cp <= 0x7FFu) {
		out.push_back(static_cast<char>(0xC0u | (cp >> 6u)));
		out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
	} else if (cp <= 0xFFFFu) {
		out.push_back(static_cast<char>(0xE0u | (cp >> 12u)));
		out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
		out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
	} else {
		out.push_back(static_cast<char>(0xF0u | (cp >> 18u)));
		out.push_back(static_cast<char>(0x80u | ((cp >> 12u) & 0x3Fu)));
		out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
		out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
	}
}

bool decode_next_utf8(std::string_view input, size_t& index, uint32_t& out_cp) noexcept
{
	if (index >= input.size()) {
		return false;
	}
	const uint8_t b0 = static_cast<uint8_t>(input[index]);
	if (b0 < 0x80u) {
		out_cp = b0;
		++index;
		return true;
	}
	if ((b0 & 0xE0u) == 0xC0u) {
		if (index + 1 >= input.size()) {
			out_cp = 0xFFFDu;
			++index;
			return true;
		}
		const uint8_t b1 = static_cast<uint8_t>(input[index + 1]);
		if ((b1 & 0xC0u) != 0x80u) {
			out_cp = 0xFFFDu;
			++index;
			return true;
		}
		const uint32_t cp = ((static_cast<uint32_t>(b0 & 0x1Fu) << 6u) |
			static_cast<uint32_t>(b1 & 0x3Fu));
		if (cp < 0x80u) {
			out_cp = 0xFFFDu;
			++index;
			return true;
		}
		out_cp = cp;
		index += 2;
		return true;
	}
	if ((b0 & 0xF0u) == 0xE0u) {
		if (index + 2 >= input.size()) {
			out_cp = 0xFFFDu;
			++index;
			return true;
		}
		const uint8_t b1 = static_cast<uint8_t>(input[index + 1]);
		const uint8_t b2 = static_cast<uint8_t>(input[index + 2]);
		if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u) {
			out_cp = 0xFFFDu;
			++index;
			return true;
		}
		const uint32_t cp = ((static_cast<uint32_t>(b0 & 0x0Fu) << 12u) |
			(static_cast<uint32_t>(b1 & 0x3Fu) << 6u) |
			static_cast<uint32_t>(b2 & 0x3Fu));
		if (cp < 0x800u || (cp >= 0xD800u && cp <= 0xDFFFu)) {
			out_cp = 0xFFFDu;
			++index;
			return true;
		}
		out_cp = cp;
		index += 3;
		return true;
	}
	if ((b0 & 0xF8u) == 0xF0u) {
		if (index + 3 >= input.size()) {
			out_cp = 0xFFFDu;
			++index;
			return true;
		}
		const uint8_t b1 = static_cast<uint8_t>(input[index + 1]);
		const uint8_t b2 = static_cast<uint8_t>(input[index + 2]);
		const uint8_t b3 = static_cast<uint8_t>(input[index + 3]);
		if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u || (b3 & 0xC0u) != 0x80u) {
			out_cp = 0xFFFDu;
			++index;
			return true;
		}
		const uint32_t cp = ((static_cast<uint32_t>(b0 & 0x07u) << 18u) |
			(static_cast<uint32_t>(b1 & 0x3Fu) << 12u) |
			(static_cast<uint32_t>(b2 & 0x3Fu) << 6u) |
			static_cast<uint32_t>(b3 & 0x3Fu));
		if (cp < 0x10000u || cp > 0x10FFFFu) {
			out_cp = 0xFFFDu;
			++index;
			return true;
		}
		out_cp = cp;
		index += 4;
		return true;
	}
	out_cp = 0xFFFDu;
	++index;
	return true;
}

std::optional<uint8_t> unicode_to_pdf_doc_encoding(uint32_t cp) noexcept
{
	if (cp == 0xFFFDu || cp > 0xFFFFu) {
		return std::nullopt;
	}
	// Only standard control codes (\t, \n, \r) and defined Table D.2 code
	// points from 0x18..0xFF map back to single-byte PDFDocEncoding.
	if (cp < 0x18u && cp != 0x09u && cp != 0x0Au && cp != 0x0Du) {
		return std::nullopt;
	}
	for (size_t b = 0x09u; b < kPdfDocEncodingToUnicode.size(); ++b) {
		if (b < 0x18u && b != 0x09u && b != 0x0Au && b != 0x0Du) {
			continue;
		}
		if (kPdfDocEncodingToUnicode[b] == static_cast<uint16_t>(cp)) {
			return static_cast<uint8_t>(b);
		}
	}
	return std::nullopt;
}

std::string_view normalize_key_view(std::string_view key) noexcept
{
	if (!key.empty() && key.front() == '/') {
		key.remove_prefix(1);
	}
	return key;
}

PdfResult<PdfValue> clone_value_recursive(
	const PdfValue& val,
	size_t current_depth,
	size_t max_depth) noexcept
{
	try {
		switch (val.kind()) {
		case PdfValueKind::Null:
			return PdfValue(PdfNull{});
		case PdfValueKind::Bool:
			return PdfValue(*val.as_bool());
		case PdfValueKind::Int:
			return PdfValue(*val.as_int());
		case PdfValueKind::Real:
			return PdfValue(*val.as_real());
		case PdfValueKind::Name:
			return PdfValue(PdfName(val.as_name_obj()->view()));
		case PdfValueKind::String: {
			const auto* s = val.as_string();
			return PdfValue(PdfString(s->bytes, s->is_hex));
		}
		case PdfValueKind::Ref:
			return PdfValue(*val.as_ref());
		case PdfValueKind::Array: {
			if (current_depth >= max_depth) {
				return make_pdf_error(
					ErrorCode::RecursionLimitExceeded,
					"PdfValue::deep_clone exceeded maximum nesting depth");
			}
			const auto* src_arr = val.as_array();
			PdfArray dst_arr;
			dst_arr.reserve(src_arr->size());
			for (const auto& elem : *src_arr) {
				RECTO_PDF_TRY(
					PdfValue cloned_elem,
					clone_value_recursive(elem, current_depth + 1, max_depth));
				dst_arr.push_back(std::move(cloned_elem));
			}
			return PdfValue(std::move(dst_arr));
		}
		case PdfValueKind::Dict: {
			if (current_depth >= max_depth) {
				return make_pdf_error(
					ErrorCode::RecursionLimitExceeded,
					"PdfValue::deep_clone exceeded maximum nesting depth");
			}
			const auto* src_dict = val.as_dict();
			PdfDict dst_dict;
			dst_dict.reserve(src_dict->size());
			for (const auto& [k, v] : src_dict->entries()) {
				RECTO_PDF_TRY(
					PdfValue cloned_val,
					clone_value_recursive(v, current_depth + 1, max_depth));
				dst_dict.set(PdfName(k.view()), std::move(cloned_val));
			}
			return PdfValue(std::move(dst_dict));
		}
		case PdfValueKind::Stream: {
			if (current_depth >= max_depth) {
				return make_pdf_error(
					ErrorCode::RecursionLimitExceeded,
					"PdfValue::deep_clone exceeded maximum nesting depth");
			}
			const auto* src_stream = val.as_stream();
			PdfDict dst_dict;
			dst_dict.reserve(src_stream->dict.size());
			for (const auto& [k, v] : src_stream->dict.entries()) {
				RECTO_PDF_TRY(
					PdfValue cloned_val,
					clone_value_recursive(v, current_depth + 1, max_depth));
				dst_dict.set(PdfName(k.view()), std::move(cloned_val));
			}
			PdfStream dst_stream(std::move(dst_dict), src_stream->raw_bytes);
			dst_stream.decoded_bytes = src_stream->decoded_bytes;
			dst_stream.file_offset = src_stream->file_offset;
			dst_stream.raw_length = src_stream->raw_length;
			return PdfValue(std::move(dst_stream));
		}
		}
		return PdfValue(PdfNull{});
	} catch (const std::bad_alloc&) {
		return PdfError{ErrorCode::OutOfMemory, "Out of memory during PdfValue::deep_clone"};
	} catch (...) {
		return PdfError{ErrorCode::InternalError, "Unexpected failure during PdfValue::deep_clone"};
	}
}

} // namespace

std::string PdfString::to_utf8() const noexcept
{
	return pdf_string_to_utf8(bytes);
}

PdfResult<PdfRect> PdfRect::from_array(const PdfArray& arr) noexcept
{
	if (arr.size() != 4) {
		return PdfError{
			ErrorCode::DamagedFormat,
			"PDF rectangle array must contain 4 numbers"};
	}
	const auto x1 = arr[0].as_number();
	const auto y1 = arr[1].as_number();
	const auto x2 = arr[2].as_number();
	const auto y2 = arr[3].as_number();
	if (!x1.has_value() || !y1.has_value() || !x2.has_value() || !y2.has_value()) {
		return PdfError{
			ErrorCode::DamagedFormat,
			"PDF rectangle array contains non-numeric or non-finite element"};
	}
	return PdfRect(*x1, *y1, *x2, *y2);
}

PdfResult<PdfMatrix> PdfMatrix::from_array(const PdfArray& arr) noexcept
{
	if (arr.size() != 6) {
		return PdfError{
			ErrorCode::DamagedFormat,
			"PDF matrix array must contain 6 numbers"};
	}
	const auto a = arr[0].as_number();
	const auto b = arr[1].as_number();
	const auto c = arr[2].as_number();
	const auto d = arr[3].as_number();
	const auto e = arr[4].as_number();
	const auto f = arr[5].as_number();
	if (!a.has_value() || !b.has_value() || !c.has_value() ||
		!d.has_value() || !e.has_value() || !f.has_value()) {
		return PdfError{
			ErrorCode::DamagedFormat,
			"PDF matrix array contains non-numeric or non-finite element"};
	}
	return PdfMatrix(*a, *b, *c, *d, *e, *f);
}

PdfDict::~PdfDict() noexcept = default;
PdfDict::PdfDict(const PdfDict& other) = default;
PdfDict& PdfDict::operator=(const PdfDict& other) = default;

void PdfDict::set(PdfName key, PdfValue value)
{
	// Per ISO 32000-1 section 7.3.7, a dictionary entry whose value is null is
	// equivalent to an absent entry.
	if (value.is_null()) {
		(void)erase(key.view());
		return;
	}
	for (auto& [k, v] : entries_) {
		if (k.view() == key.view()) {
			v = std::move(value);
			return;
		}
	}
	entries_.emplace_back(std::move(key), std::move(value));
}

void PdfDict::set(std::string_view key, PdfValue value)
{
	set(PdfName(key), std::move(value));
}

bool PdfDict::erase(std::string_view key) noexcept
{
	const std::string_view norm = normalize_key_view(key);
	for (auto it = entries_.begin(); it != entries_.end(); ++it) {
		if (it->first.view() == norm) {
			entries_.erase(it);
			return true;
		}
	}
	return false;
}

bool PdfDict::contains(std::string_view key) const noexcept
{
	return get(key) != nullptr;
}

const PdfValue* PdfDict::get(std::string_view key) const noexcept
{
	const std::string_view norm = normalize_key_view(key);
	for (const auto& [k, v] : entries_) {
		if (k.view() == norm) {
			return &v;
		}
	}
	return nullptr;
}

PdfValue* PdfDict::get_mut(std::string_view key) noexcept
{
	const std::string_view norm = normalize_key_view(key);
	for (auto& [k, v] : entries_) {
		if (k.view() == norm) {
			return &v;
		}
	}
	return nullptr;
}

std::optional<bool> PdfDict::get_bool(std::string_view key) const noexcept
{
	if (const auto* v = get(key)) {
		return v->as_bool();
	}
	return std::nullopt;
}

std::optional<int64_t> PdfDict::get_int(std::string_view key) const noexcept
{
	if (const auto* v = get(key)) {
		return v->as_int();
	}
	return std::nullopt;
}

std::optional<double> PdfDict::get_real(std::string_view key) const noexcept
{
	if (const auto* v = get(key)) {
		return v->as_real();
	}
	return std::nullopt;
}

std::optional<double> PdfDict::get_number(std::string_view key) const noexcept
{
	if (const auto* v = get(key)) {
		return v->as_number();
	}
	return std::nullopt;
}

std::optional<std::string_view> PdfDict::get_name(std::string_view key) const noexcept
{
	if (const auto* v = get(key)) {
		return v->as_name();
	}
	return std::nullopt;
}

const PdfString* PdfDict::get_string(std::string_view key) const noexcept
{
	if (const auto* v = get(key)) {
		return v->as_string();
	}
	return nullptr;
}

const PdfArray* PdfDict::get_array(std::string_view key) const noexcept
{
	if (const auto* v = get(key)) {
		return v->as_array();
	}
	return nullptr;
}

PdfArray* PdfDict::get_array_mut(std::string_view key) noexcept
{
	if (auto* v = get_mut(key)) {
		return v->as_array_mut();
	}
	return nullptr;
}

const PdfDict* PdfDict::get_dict(std::string_view key) const noexcept
{
	if (const auto* v = get(key)) {
		return v->as_dict();
	}
	return nullptr;
}

PdfDict* PdfDict::get_dict_mut(std::string_view key) noexcept
{
	if (auto* v = get_mut(key)) {
		return v->as_dict_mut();
	}
	return nullptr;
}

const PdfStream* PdfDict::get_stream(std::string_view key) const noexcept
{
	if (const auto* v = get(key)) {
		return v->as_stream();
	}
	return nullptr;
}

std::optional<IndirectRef> PdfDict::get_ref(std::string_view key) const noexcept
{
	if (const auto* v = get(key)) {
		return v->as_ref();
	}
	return std::nullopt;
}

std::optional<PdfRect> PdfDict::get_rect(std::string_view key) const noexcept
{
	if (const auto* arr = get_array(key)) {
		const auto res = PdfRect::from_array(*arr);
		if (res.has_value()) {
			return res.value();
		}
	}
	return std::nullopt;
}

std::optional<PdfMatrix> PdfDict::get_matrix(std::string_view key) const noexcept
{
	if (const auto* arr = get_array(key)) {
		const auto res = PdfMatrix::from_array(*arr);
		if (res.has_value()) {
			return res.value();
		}
	}
	return std::nullopt;
}

bool PdfDict::operator==(const PdfDict& other) const noexcept
{
	if (entries_.size() != other.entries_.size()) {
		return false;
	}
	for (const auto& [k, v] : entries_) {
		const auto* rhs_val = other.get(k.view());
		if (!rhs_val || !(*rhs_val == v)) {
			return false;
		}
	}
	return true;
}

PdfResult<PdfRect> PdfValue::as_rect() const noexcept
{
	const auto* arr = as_array();
	if (!arr) {
		return PdfError{
			ErrorCode::DamagedFormat,
			"Expected PDF array for PdfRect"};
	}
	return PdfRect::from_array(*arr);
}

PdfResult<PdfMatrix> PdfValue::as_matrix() const noexcept
{
	const auto* arr = as_array();
	if (!arr) {
		return PdfError{
			ErrorCode::DamagedFormat,
			"Expected PDF array for PdfMatrix"};
	}
	return PdfMatrix::from_array(*arr);
}

PdfResult<PdfValue> PdfValue::deep_clone(size_t max_depth) const noexcept
{
	return clone_value_recursive(*this, 0, max_depth);
}

std::string pdf_string_to_utf8(std::span<const uint8_t> bytes) noexcept
{
	try {
		if (bytes.empty()) {
			return {};
		}

		// 1. UTF-16BE BOM: 0xFE 0xFF (ISO 32000-1 section 7.9.2.2)
		if (bytes.size() >= 2 && bytes[0] == 0xFEu && bytes[1] == 0xFFu) {
			std::string out;
			out.reserve(bytes.size());
			size_t i = 2;
			while (i + 1 < bytes.size()) {
				const uint16_t u1 = static_cast<uint16_t>(
					(static_cast<uint16_t>(bytes[i]) << 8u) |
					static_cast<uint16_t>(bytes[i + 1]));
				i += 2;

				// ISO 32000-1 section 7.9.2.2.2: 0x001B language escape sequence
				// is delimited by a matching 0x001B code unit.
				if (u1 == 0x001Bu) {
					while (i + 1 < bytes.size()) {
						const uint16_t esc_unit = static_cast<uint16_t>(
							(static_cast<uint16_t>(bytes[i]) << 8u) |
							static_cast<uint16_t>(bytes[i + 1]));
						i += 2;
						if (esc_unit == 0x001Bu) {
							break;
						}
					}
					continue;
				}

				if (u1 >= 0xD800u && u1 <= 0xDBFFu) {
					if (i + 1 < bytes.size()) {
						const uint16_t u2 = static_cast<uint16_t>(
							(static_cast<uint16_t>(bytes[i]) << 8u) |
							static_cast<uint16_t>(bytes[i + 1]));
						if (u2 >= 0xDC00u && u2 <= 0xDFFFu) {
							i += 2;
							const uint32_t cp = 0x10000u +
								((static_cast<uint32_t>(u1 - 0xD800u) << 10u) |
								 static_cast<uint32_t>(u2 - 0xDC00u));
							append_utf8_codepoint(out, cp);
							continue;
						}
					}
					append_utf8_codepoint(out, 0xFFFDu);
				} else if (u1 >= 0xDC00u && u1 <= 0xDFFFu) {
					append_utf8_codepoint(out, 0xFFFDu);
				} else {
					append_utf8_codepoint(out, u1);
				}
			}
			if (i < bytes.size()) {
				append_utf8_codepoint(out, 0xFFFDu);
			}
			return out;
		}

		// 2. UTF-8 BOM: 0xEF 0xBB 0xBF (ISO 32000-2 section 7.9.2.2)
		if (bytes.size() >= 3 &&
			bytes[0] == 0xEFu && bytes[1] == 0xBBu && bytes[2] == 0xBFu) {
			const std::string_view utf8_body(
				reinterpret_cast<const char*>(bytes.data() + 3),
				bytes.size() - 3);
			std::string out;
			out.reserve(utf8_body.size());
			size_t idx = 0;
			uint32_t cp = 0;
			while (decode_next_utf8(utf8_body, idx, cp)) {
				append_utf8_codepoint(out, cp);
			}
			return out;
		}

		// 3. PDFDocEncoding (ISO 32000-1 Table D.2)
		std::string out;
		out.reserve(bytes.size());
		for (const uint8_t b : bytes) {
			append_utf8_codepoint(out, kPdfDocEncodingToUnicode[b]);
		}
		return out;
	} catch (...) {
		return {};
	}
}

PdfString utf8_to_pdf_string(std::string_view utf8) noexcept
{
	try {
		if (utf8.empty()) {
			return PdfString{};
		}

		std::vector<uint32_t> codepoints;
		codepoints.reserve(utf8.size());
		size_t idx = 0;
		uint32_t cp = 0;
		bool all_pdf_doc = true;
		while (decode_next_utf8(utf8, idx, cp)) {
			codepoints.push_back(cp);
			if (!unicode_to_pdf_doc_encoding(cp).has_value()) {
				all_pdf_doc = false;
			}
		}

		if (all_pdf_doc) {
			std::vector<uint8_t> pdf_doc_bytes;
			pdf_doc_bytes.reserve(codepoints.size());
			for (const uint32_t c : codepoints) {
				pdf_doc_bytes.push_back(*unicode_to_pdf_doc_encoding(c));
			}
			// Guard against accidental BOM collision with UTF-16BE (FE FF) or
			// UTF-8 (EF BB BF) at the start of a PDFDocEncoding string.
			const bool collides_utf16_bom =
				pdf_doc_bytes.size() >= 2 &&
				pdf_doc_bytes[0] == 0xFEu &&
				pdf_doc_bytes[1] == 0xFFu;
			const bool collides_utf8_bom =
				pdf_doc_bytes.size() >= 3 &&
				pdf_doc_bytes[0] == 0xEFu &&
				pdf_doc_bytes[1] == 0xBBu &&
				pdf_doc_bytes[2] == 0xBFu;
			if (!collides_utf16_bom && !collides_utf8_bom) {
				return PdfString(std::move(pdf_doc_bytes), false);
			}
		}

		// Encode as UTF-16BE with leading 0xFE 0xFF BOM.
		std::vector<uint8_t> utf16_bytes;
		utf16_bytes.reserve(2 + codepoints.size() * 2);
		utf16_bytes.push_back(0xFEu);
		utf16_bytes.push_back(0xFFu);
		for (uint32_t c : codepoints) {
			if (c > 0x10FFFFu || (c >= 0xD800u && c <= 0xDFFFu)) {
				c = 0xFFFDu;
			}
			if (c <= 0xFFFFu) {
				utf16_bytes.push_back(static_cast<uint8_t>((c >> 8u) & 0xFFu));
				utf16_bytes.push_back(static_cast<uint8_t>(c & 0xFFu));
			} else {
				const uint32_t v = c - 0x10000u;
				const uint16_t high = static_cast<uint16_t>(0xD800u + (v >> 10u));
				const uint16_t low = static_cast<uint16_t>(0xDC00u + (v & 0x3FFu));
				utf16_bytes.push_back(static_cast<uint8_t>((high >> 8u) & 0xFFu));
				utf16_bytes.push_back(static_cast<uint8_t>(high & 0xFFu));
				utf16_bytes.push_back(static_cast<uint8_t>((low >> 8u) & 0xFFu));
				utf16_bytes.push_back(static_cast<uint8_t>(low & 0xFFu));
			}
		}
		return PdfString(std::move(utf16_bytes), false);
	} catch (...) {
		return PdfString{};
	}
}

} // namespace recto::pdf
