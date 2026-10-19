// PDF document model (`recto::pdf::Document`): page-tree resolver with
// inherited attributes and cycle protection, `/Info` and XMP `/Metadata`
// extractor, `/Outlines` and named-destination resolver, `/Annots` `/Link`
// extractor (with inert `/Launch` and `/JavaScript` refusal), and deduplicated
// document font inventory (ISO 32000-1:2008 section 7.7, 12.3, 12.5, 14.3;
// D01 T01 section 10).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "document.h"

#include "lexer.h"
#include "parser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <unordered_set>
#include <utility>

namespace recto::pdf {

namespace {

[[nodiscard]] PdfError make_doc_error(
	ErrorCode code,
	DiagnosticId /*diag_id*/,
	std::string_view message,
	std::optional<uint64_t> byte_offset = std::nullopt,
	std::optional<uint32_t> object_num = std::nullopt,
	uint16_t generation_num = 0) noexcept
{
	return make_pdf_error(code, message, byte_offset, object_num, generation_num);
}

[[nodiscard]] constexpr bool is_ascii_digit(char ch) noexcept
{
	return ch >= '0' && ch <= '9';
}

[[nodiscard]] constexpr bool is_ascii_upper(char ch) noexcept
{
	return ch >= 'A' && ch <= 'Z';
}

[[nodiscard]] constexpr char to_ascii_lower(char ch) noexcept
{
	return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
}

[[nodiscard]] bool parse_fixed_digits(
	std::string_view sv,
	size_t offset,
	size_t count,
	int32_t& out_val) noexcept
{
	if (offset + count > sv.size()) {
		return false;
	}
	int32_t acc = 0;
	for (size_t i = 0; i < count; ++i) {
		const char ch = sv[offset + i];
		if (!is_ascii_digit(ch)) {
			return false;
		}
		acc = acc * 10 + static_cast<int32_t>(ch - '0');
	}
	out_val = acc;
	return true;
}

[[nodiscard]] constexpr int32_t days_in_month(int32_t year, int32_t month) noexcept
{
	switch (month) {
	case 1:
	case 3:
	case 5:
	case 7:
	case 8:
	case 10:
	case 12:
		return 31;
	case 4:
	case 6:
	case 9:
	case 11:
		return 30;
	case 2: {
		const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
		return leap ? 29 : 28;
	}
	default:
		return 0;
	}
}

[[nodiscard]] std::string_view trim_ascii_whitespace(std::string_view sv) noexcept
{
	while (!sv.empty() &&
		   (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
		sv.remove_prefix(1);
	}
	while (!sv.empty() &&
		   (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
		sv.remove_suffix(1);
	}
	return sv;
}

[[nodiscard]] std::optional<PdfDate> parse_iso8601_date(
	std::string_view sv,
	std::string_view original_raw)
{
	// Expect at least `YYYY-MM-DD` (10 characters).
	if (sv.size() < 10u || sv[4] != '-' || sv[7] != '-') {
		return std::nullopt;
	}
	PdfDate dt{};
	if (!parse_fixed_digits(sv, 0, 4, dt.year) ||
		!parse_fixed_digits(sv, 5, 2, dt.month) ||
		!parse_fixed_digits(sv, 8, 2, dt.day)) {
		return std::nullopt;
	}
	if (dt.year < 1 || dt.month < 1 || dt.month > 12 ||
		dt.day < 1 || dt.day > days_in_month(dt.year, dt.month)) {
		return std::nullopt;
	}

	size_t pos = 10u;
	if (pos < sv.size() && (sv[pos] == 'T' || sv[pos] == 't' || sv[pos] == ' ')) {
		++pos;
		if (pos + 5u > sv.size() || sv[pos + 2u] != ':') {
			return std::nullopt;
		}
		if (!parse_fixed_digits(sv, pos, 2, dt.hour) ||
			!parse_fixed_digits(sv, pos + 3u, 2, dt.minute)) {
			return std::nullopt;
		}
		pos += 5u;
		if (pos < sv.size() && sv[pos] == ':') {
			++pos;
			if (!parse_fixed_digits(sv, pos, 2, dt.second)) {
				return std::nullopt;
			}
			pos += 2u;
			if (pos < sv.size() && sv[pos] == '.') {
				++pos;
				while (pos < sv.size() && is_ascii_digit(sv[pos])) {
					++pos;
				}
			}
		}
		if (dt.hour < 0 || dt.hour > 23 ||
			dt.minute < 0 || dt.minute > 59 ||
			dt.second < 0 || dt.second > 59) {
			return std::nullopt;
		}
	}

	if (pos < sv.size()) {
		const char tz_ch = sv[pos];
		if (tz_ch == 'Z' || tz_ch == 'z') {
			dt.has_timezone = true;
			dt.utc_offset_minutes = 0;
			++pos;
		} else if (tz_ch == '+' || tz_ch == '-') {
			const int32_t sign = (tz_ch == '-') ? -1 : 1;
			++pos;
			int32_t tz_h = 0;
			int32_t tz_m = 0;
			if (!parse_fixed_digits(sv, pos, 2, tz_h)) {
				return std::nullopt;
			}
			pos += 2u;
			if (pos < sv.size() && sv[pos] == ':') {
				++pos;
			}
			if (pos + 2u <= sv.size() && is_ascii_digit(sv[pos])) {
				if (!parse_fixed_digits(sv, pos, 2, tz_m)) {
					return std::nullopt;
				}
				pos += 2u;
			}
			if (tz_h < 0 || tz_h > 23 || tz_m < 0 || tz_m > 59) {
				return std::nullopt;
			}
			dt.has_timezone = true;
			dt.utc_offset_minutes = sign * (tz_h * 60 + tz_m);
		} else {
			return std::nullopt;
		}
	}
	if (pos != sv.size()) {
		return std::nullopt;
	}

	detail::check_crypto_alloc();
	dt.raw.assign(original_raw.data(), original_raw.size());
	detail::check_crypto_alloc();
	dt.iso8601 = dt.to_iso8601();
	return dt;
}

[[nodiscard]] bool is_valid_pdf_version_string(std::string_view ver) noexcept
{
	return ver.size() == 3u &&
		ver[1] == '.' &&
		((ver[0] == '1' && ver[2] >= '0' && ver[2] <= '7') ||
		 (ver[0] == '2' && ver[2] == '0'));
}

[[nodiscard]] std::string decode_xml_entities(std::string_view text)
{
	std::string out;
	out.reserve(text.size());
	size_t i = 0;
	while (i < text.size()) {
		if (text[i] == '&') {
			const size_t semi = text.find(';', i + 1u);
			if (semi != std::string_view::npos && semi - i <= 10u) {
				const std::string_view ent = text.substr(i + 1u, semi - i - 1u);
				if (ent == "amp") {
					out.push_back('&');
					i = semi + 1u;
					continue;
				}
				if (ent == "lt") {
					out.push_back('<');
					i = semi + 1u;
					continue;
				}
				if (ent == "gt") {
					out.push_back('>');
					i = semi + 1u;
					continue;
				}
				if (ent == "quot") {
					out.push_back('"');
					i = semi + 1u;
					continue;
				}
				if (ent == "apos") {
					out.push_back('\'');
					i = semi + 1u;
					continue;
				}
				if (!ent.empty() && ent.front() == '#') {
					uint32_t cp = 0;
					bool ok = false;
					if (ent.size() > 2u && (ent[1] == 'x' || ent[1] == 'X')) {
						const auto sub = ent.substr(2u);
						auto [ptr, ec] = std::from_chars(
							sub.data(),
							sub.data() + sub.size(),
							cp,
							16);
						ok = (ec == std::errc{} && ptr == sub.data() + sub.size());
					} else if (ent.size() > 1u) {
						const auto sub = ent.substr(1u);
						auto [ptr, ec] = std::from_chars(
							sub.data(),
							sub.data() + sub.size(),
							cp,
							10);
						ok = (ec == std::errc{} && ptr == sub.data() + sub.size());
					}
					if (ok && cp > 0u && cp <= 0x10FFFFu && (cp < 0xD800u || cp > 0xDFFFu)) {
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
						i = semi + 1u;
						continue;
					}
				}
			}
		}
		out.push_back(text[i]);
		++i;
	}
	return out;
}

[[nodiscard]] std::string strip_xml_tags(std::string_view xml_fragment)
{
	std::string out;
	out.reserve(xml_fragment.size());
	bool in_tag = false;
	for (const char ch : xml_fragment) {
		if (ch == '<') {
			in_tag = true;
			continue;
		}
		if (ch == '>') {
			in_tag = false;
			continue;
		}
		if (!in_tag) {
			out.push_back(ch);
		}
	}
	return decode_xml_entities(trim_ascii_whitespace(out));
}

// Extracts an XMP property value from `<tag ...>...</tag>` or attribute
// `tag="value"` in an XMP metadata XML packet. When the element contains
// `<rdf:li>` children (`rdf:Alt`, `rdf:Seq`, `rdf:Bag`), collects the `<rdf:li>`
// items (joining multiple items in `rdf:Seq` / `rdf:Bag` with `", "`, or taking
// the `x-default` / first item in `rdf:Alt`).
[[nodiscard]] std::string extract_xmp_tag_value(
	std::string_view xml,
	std::string_view tag_name)
{
	// 1. Check for `<tag_name...>` element:
	const std::string open_prefix = "<" + std::string(tag_name);
	const std::string close_tag = "</" + std::string(tag_name) + ">";
	size_t search_pos = 0;
	while (search_pos < xml.size()) {
		const size_t open_pos = xml.find(open_prefix, search_pos);
		if (open_pos == std::string_view::npos) {
			break;
		}
		const size_t after_name = open_pos + open_prefix.size();
		if (after_name < xml.size()) {
			const char next_ch = xml[after_name];
			if (next_ch != '>' && next_ch != ' ' && next_ch != '\t' &&
				next_ch != '\r' && next_ch != '\n' && next_ch != '/') {
				search_pos = after_name;
				continue;
			}
		}
		const size_t tag_end = xml.find('>', after_name);
		if (tag_end == std::string_view::npos) {
			break;
		}
		if (tag_end > open_pos && xml[tag_end - 1u] == '/') {
			search_pos = tag_end + 1u;
			continue;
		}
		const size_t close_pos = xml.find(close_tag, tag_end + 1u);
		if (close_pos == std::string_view::npos) {
			break;
		}
		const std::string_view inner = xml.substr(tag_end + 1u, close_pos - (tag_end + 1u));
		const bool is_alt = (inner.find("<rdf:Alt") != std::string_view::npos);

		// Collect `<rdf:li...>` items if present:
		std::vector<std::string> li_values;
		size_t li_pos = 0;
		while (li_pos < inner.size()) {
			const size_t li_open = inner.find("<rdf:li", li_pos);
			if (li_open == std::string_view::npos) {
				break;
			}
			const size_t li_hdr_end = inner.find('>', li_open + 7u);
			if (li_hdr_end == std::string_view::npos) {
				break;
			}
			const size_t li_close = inner.find("</rdf:li>", li_hdr_end + 1u);
			if (li_close == std::string_view::npos) {
				break;
			}
			std::string item = strip_xml_tags(
				inner.substr(li_hdr_end + 1u, li_close - (li_hdr_end + 1u)));
			if (!item.empty()) {
				li_values.push_back(std::move(item));
				if (is_alt) {
					break;
				}
			}
			li_pos = li_close + 9u;
		}

		if (!li_values.empty()) {
			std::string joined = std::move(li_values[0]);
			for (size_t i = 1; i < li_values.size(); ++i) {
				joined += ", ";
				joined += li_values[i];
			}
			return joined;
		}

		std::string direct = strip_xml_tags(inner);
		if (!direct.empty()) {
			return direct;
		}
		search_pos = close_pos + close_tag.size();
	}

	// 2. Check for shorthand attribute `tag_name="..."`:
	const std::string attr_prefix = std::string(tag_name) + "=";
	search_pos = 0;
	while (search_pos < xml.size()) {
		const size_t attr_pos = xml.find(attr_prefix, search_pos);
		if (attr_pos == std::string_view::npos) {
			break;
		}
		if (attr_pos > 0) {
			const char prev = xml[attr_pos - 1u];
			if (prev != ' ' && prev != '\t' && prev != '\r' && prev != '\n') {
				search_pos = attr_pos + attr_prefix.size();
				continue;
			}
		}
		const size_t val_start = attr_pos + attr_prefix.size();
		if (val_start < xml.size() && (xml[val_start] == '"' || xml[val_start] == '\'')) {
			const char quote = xml[val_start];
			const size_t val_end = xml.find(quote, val_start + 1u);
			if (val_end != std::string_view::npos) {
				return decode_xml_entities(
					trim_ascii_whitespace(xml.substr(val_start + 1u, val_end - val_start - 1u)));
			}
		}
		search_pos = val_start;
	}

	return {};
}

[[nodiscard]] bool has_subset_font_prefix(std::string_view font_name) noexcept
{
	if (font_name.size() <= 7u || font_name[6] != '+') {
		return false;
	}
	for (size_t i = 0; i < 6u; ++i) {
		if (!is_ascii_upper(font_name[i])) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool is_safe_external_uri(std::string_view uri) noexcept
{
	const std::string_view trimmed = trim_ascii_whitespace(uri);
	if (trimmed.empty()) {
		return false;
	}
	for (const char ch : trimmed) {
		if (static_cast<unsigned char>(ch) < 0x20u) {
			return false;
		}
	}
	auto starts_with_ci = [](std::string_view s, std::string_view prefix) noexcept -> bool {
		if (s.size() < prefix.size()) {
			return false;
		}
		for (size_t i = 0; i < prefix.size(); ++i) {
			if (to_ascii_lower(s[i]) != prefix[i]) {
				return false;
			}
		}
		return true;
	};
	return starts_with_ci(trimmed, "http://") ||
		starts_with_ci(trimmed, "https://") ||
		starts_with_ci(trimmed, "mailto:");
}

[[nodiscard]] bool is_refused_action_name(std::string_view action_s) noexcept
{
	return action_s == "Launch" ||
		action_s == "JavaScript" ||
		action_s == "SubmitForm" ||
		action_s == "ResetForm" ||
		action_s == "ImportData" ||
		action_s == "Movie" ||
		action_s == "Sound" ||
		action_s == "Rendition" ||
		action_s == "Trans" ||
		action_s == "GoTo3DView" ||
		action_s == "RichMediaExecute" ||
		action_s == "SetOCGState" ||
		action_s == "Thread";
}

[[nodiscard]] PdfRect intersect_or_keep_box(const PdfRect& media_box, const PdfRect& box) noexcept
{
	const double x0 = std::max(media_box.x_min, box.x_min);
	const double y0 = std::max(media_box.y_min, box.y_min);
	const double x1 = std::min(media_box.x_max, box.x_max);
	const double y1 = std::min(media_box.y_max, box.y_max);
	if (x1 > x0 && y1 > y0) {
		return PdfRect(x0, y0, x1, y1);
	}
	return media_box;
}

} // namespace

std::string PdfDate::to_iso8601() const
{
	char buf[64]{};
	if (!has_timezone) {
		std::snprintf(
			buf,
			sizeof(buf),
			"%04d-%02d-%02dT%02d:%02d:%02d",
			year,
			month,
			day,
			hour,
			minute,
			second);
		return std::string(buf);
	}
	if (utc_offset_minutes == 0) {
		std::snprintf(
			buf,
			sizeof(buf),
			"%04d-%02d-%02dT%02d:%02d:%02dZ",
			year,
			month,
			day,
			hour,
			minute,
			second);
		return std::string(buf);
	}
	const char sign = (utc_offset_minutes < 0) ? '-' : '+';
	const int32_t abs_off = std::abs(utc_offset_minutes);
	const int32_t tz_h = abs_off / 60;
	const int32_t tz_m = abs_off % 60;
	std::snprintf(
		buf,
		sizeof(buf),
		"%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
		year,
		month,
		day,
		hour,
		minute,
		second,
		sign,
		tz_h,
		tz_m);
	return std::string(buf);
}

std::optional<PdfDate> parse_pdf_date(std::string_view raw) noexcept
{
	try {
		const std::string_view trimmed = trim_ascii_whitespace(raw);
		if (trimmed.empty()) {
			return std::nullopt;
		}

		// Support ISO-8601 XMP date strings (`YYYY-MM-DD...`) when not prefixed by `D:`:
		if (!trimmed.starts_with("D:") && trimmed.size() >= 10u &&
			trimmed[4] == '-' && trimmed[7] == '-') {
			return parse_iso8601_date(trimmed, trimmed);
		}

		std::string_view s = trimmed;
		if (s.starts_with("D:")) {
			s.remove_prefix(2u);
		}

		PdfDate dt{};
		if (!parse_fixed_digits(s, 0u, 4u, dt.year) || dt.year < 1) {
			return std::nullopt;
		}
		size_t pos = 4u;

		auto parse_opt_pair = [&](int32_t& field) noexcept -> bool {
			if (pos >= s.size() || !is_ascii_digit(s[pos])) {
				return true;
			}
			if (!parse_fixed_digits(s, pos, 2u, field)) {
				return false;
			}
			pos += 2u;
			return true;
		};

		if (!parse_opt_pair(dt.month) ||
			!parse_opt_pair(dt.day) ||
			!parse_opt_pair(dt.hour) ||
			!parse_opt_pair(dt.minute) ||
			!parse_opt_pair(dt.second)) {
			return std::nullopt;
		}

		if (dt.month < 1 || dt.month > 12 ||
			dt.day < 1 || dt.day > days_in_month(dt.year, dt.month) ||
			dt.hour < 0 || dt.hour > 23 ||
			dt.minute < 0 || dt.minute > 59 ||
			dt.second < 0 || dt.second > 59) {
			return std::nullopt;
		}

		if (pos < s.size()) {
			const char o_ch = s[pos];
			if (o_ch == 'Z' || o_ch == 'z') {
				dt.has_timezone = true;
				dt.utc_offset_minutes = 0;
				++pos;
				// Some PDF producers emit `D:YYYYMMDDHHmmSSZ00'00'`; accept trailing zero offset.
				if (pos < s.size()) {
					int32_t zh = 0;
					int32_t zm = 0;
					if (!parse_fixed_digits(s, pos, 2u, zh) || zh != 0) {
						return std::nullopt;
					}
					pos += 2u;
					if (pos < s.size() && (s[pos] == '\'' || s[pos] == ':')) {
						++pos;
					}
					if (pos < s.size() && is_ascii_digit(s[pos])) {
						if (!parse_fixed_digits(s, pos, 2u, zm) || zm != 0) {
							return std::nullopt;
						}
						pos += 2u;
					}
					if (pos < s.size() && s[pos] == '\'') {
						++pos;
					}
				}
			} else if (o_ch == '+' || o_ch == '-') {
				const int32_t sign = (o_ch == '-') ? -1 : 1;
				++pos;
				int32_t tz_h = 0;
				int32_t tz_m = 0;
				if (!parse_fixed_digits(s, pos, 2u, tz_h)) {
					return std::nullopt;
				}
				pos += 2u;
				if (pos < s.size() && (s[pos] == '\'' || s[pos] == ':')) {
					++pos;
				}
				if (pos < s.size() && is_ascii_digit(s[pos])) {
					if (!parse_fixed_digits(s, pos, 2u, tz_m)) {
						return std::nullopt;
					}
					pos += 2u;
				}
				if (pos < s.size() && s[pos] == '\'') {
					++pos;
				}
				if (tz_h < 0 || tz_h > 23 || tz_m < 0 || tz_m > 59) {
					return std::nullopt;
				}
				dt.has_timezone = true;
				dt.utc_offset_minutes = sign * (tz_h * 60 + tz_m);
			} else {
				return std::nullopt;
			}
		}

		if (pos != s.size()) {
			return std::nullopt;
		}

		detail::check_crypto_alloc();
		dt.raw.assign(trimmed.data(), trimmed.size());
		detail::check_crypto_alloc();
		dt.iso8601 = dt.to_iso8601();
		return dt;
	} catch (...) {
		return std::nullopt;
	}
}

PdfResult<std::vector<uint8_t>> Document::read_file_bytes(
	const std::filesystem::path& path) noexcept
{
	try {
		if (path.empty()) {
			return make_doc_error(
				ErrorCode::FileNotFound,
				DiagnosticId::FileNotFound,
				"Cannot open PDF with empty file path");
		}

		const HANDLE h_file = ::CreateFileW(
			path.c_str(),
			GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (h_file == INVALID_HANDLE_VALUE) {
			const DWORD err = ::GetLastError();
			if (err == ERROR_FILE_NOT_FOUND ||
				err == ERROR_PATH_NOT_FOUND ||
				err == ERROR_INVALID_NAME ||
				err == ERROR_BAD_NETPATH) {
				return make_doc_error(
					ErrorCode::FileNotFound,
					DiagnosticId::FileNotFound,
					"PDF file not found on disk");
			}
			if (err == ERROR_ACCESS_DENIED ||
				err == ERROR_SHARING_VIOLATION ||
				err == ERROR_LOCK_VIOLATION) {
				return make_doc_error(
					ErrorCode::AccessDenied,
					DiagnosticId::AccessDenied,
					"Access denied while opening PDF file");
			}
			return make_doc_error(
				ErrorCode::IoError,
				DiagnosticId::IoError,
				"Win32 CreateFileW failed while opening PDF file");
		}

		struct HandleCloser {
			HANDLE h{INVALID_HANDLE_VALUE};
			~HandleCloser() noexcept
			{
				if (h != INVALID_HANDLE_VALUE) {
					::CloseHandle(h);
				}
			}
		} closer{h_file};

		LARGE_INTEGER file_size_li{};
		if (::GetFileSizeEx(h_file, &file_size_li) == FALSE || file_size_li.QuadPart < 0) {
			return make_doc_error(
				ErrorCode::IoError,
				DiagnosticId::IoError,
				"GetFileSizeEx failed while reading PDF file");
		}

		const uint64_t total_size = static_cast<uint64_t>(file_size_li.QuadPart);
		if (total_size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
			return make_doc_error(
				ErrorCode::OutOfMemory,
				DiagnosticId::OutOfMemory,
				"PDF file size exceeds addressable memory");
		}

		std::vector<uint8_t> buffer(static_cast<size_t>(total_size));
		uint64_t total_read = 0;
		while (total_read < total_size) {
			const uint64_t rem = total_size - total_read;
			const DWORD chunk = static_cast<DWORD>(
				std::min<uint64_t>(rem, 64ULL * 1024ULL * 1024ULL));
			DWORD bytes_read = 0;
			if (::ReadFile(h_file, buffer.data() + total_read, chunk, &bytes_read, nullptr) == FALSE ||
				bytes_read == 0) {
				return make_doc_error(
					ErrorCode::IoError,
					DiagnosticId::IoError,
					"ReadFile failed or returned short read on PDF file");
			}
			total_read += bytes_read;
		}

		return buffer;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while reading PDF file into memory");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while reading PDF file");
	}
}

PdfResult<std::unique_ptr<Document>> Document::open(
	const std::filesystem::path& path) noexcept
{
	RECTO_PDF_TRY(std::vector<uint8_t> bytes, read_file_bytes(path));
	return open_from_owned_bytes(std::move(bytes), std::nullopt, /*allow_unauthenticated=*/false);
}

PdfResult<std::unique_ptr<Document>> Document::open(
	const std::filesystem::path& path,
	std::string_view password) noexcept
{
	RECTO_PDF_TRY(std::vector<uint8_t> bytes, read_file_bytes(path));
	return open_from_owned_bytes(std::move(bytes), password, /*allow_unauthenticated=*/false);
}

PdfResult<std::unique_ptr<Document>> Document::open_memory(
	std::span<const uint8_t> bytes) noexcept
{
	try {
		std::vector<uint8_t> owned(bytes.begin(), bytes.end());
		return open_from_owned_bytes(std::move(owned), std::nullopt, /*allow_unauthenticated=*/false);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while copying PDF memory buffer");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while copying PDF memory buffer");
	}
}

PdfResult<std::unique_ptr<Document>> Document::open_memory(
	std::span<const uint8_t> bytes,
	std::string_view password) noexcept
{
	try {
		std::vector<uint8_t> owned(bytes.begin(), bytes.end());
		return open_from_owned_bytes(std::move(owned), password, /*allow_unauthenticated=*/false);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while copying PDF memory buffer");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while copying PDF memory buffer");
	}
}

PdfResult<std::unique_ptr<Document>> Document::authenticate(
	const std::filesystem::path& path,
	std::string_view password) noexcept
{
	return open(path, password);
}

PdfResult<std::unique_ptr<Document>> Document::authenticate(
	std::span<const uint8_t> bytes,
	std::string_view password) noexcept
{
	return open_memory(bytes, password);
}

PdfResult<std::unique_ptr<Document>> Document::open_unauthenticated(
	const std::filesystem::path& path) noexcept
{
	RECTO_PDF_TRY(std::vector<uint8_t> bytes, read_file_bytes(path));
	return open_from_owned_bytes(std::move(bytes), std::nullopt, /*allow_unauthenticated=*/true);
}

PdfResult<std::unique_ptr<Document>> Document::open_memory_unauthenticated(
	std::span<const uint8_t> bytes) noexcept
{
	try {
		std::vector<uint8_t> owned(bytes.begin(), bytes.end());
		return open_from_owned_bytes(std::move(owned), std::nullopt, /*allow_unauthenticated=*/true);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while copying PDF memory buffer");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while copying PDF memory buffer");
	}
}

PdfResult<std::unique_ptr<Document>> Document::open_from_owned_bytes(
	std::vector<uint8_t>&& bytes,
	std::optional<std::string_view> password,
	bool allow_unauthenticated) noexcept
{
	try {
		RECTO_ETW_SPAN("Document.Open");
		auto doc = std::make_unique<Document>();
		doc->file_bytes_ = std::move(bytes);
		RECTO_PDF_RETURN_IF_ERROR(
			doc->initialize_xref_and_security(password, allow_unauthenticated));
		if (doc->description_.is_authenticated) {
			RECTO_PDF_RETURN_IF_ERROR(doc->load_document_structure());
		}
		return doc;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while opening PDF document");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while opening PDF document");
	}
}

PdfResult<void> Document::initialize_xref_and_security(
	std::optional<std::string_view> password,
	bool allow_unauthenticated)
{
	RECTO_PDF_TRY(
		XrefTable parsed_xref,
		XrefTable::parse(file_bytes_, kMaxXrefTotalEntries, /*allow_recovery=*/true));
	xref_ = std::move(parsed_xref);
	description_.pdf_version = std::string(xref_.pdf_version());
	description_.is_recovered = xref_.recovered();

	RECTO_PDF_TRY(
		std::optional<SecurityHandler> sec,
		SecurityHandler::from_xref(xref_, file_bytes_));
	security_handler_ = std::move(sec);

	if (security_handler_.has_value()) {
		description_.is_encrypted = true;
		description_.encryption_method = security_handler_->params().stream_method;
		description_.encryption_v = security_handler_->params().v;
		description_.encryption_r = security_handler_->params().r;
		description_.encryption_key_bits = security_handler_->params().key_length_bits;
		description_.permissions = security_handler_->permissions();

		if (password.has_value() && (!security_handler_->is_authenticated() || !password->empty())) {
			RECTO_PDF_RETURN_IF_ERROR(security_handler_->authenticate(*password));
		}

		description_.is_authenticated = security_handler_->is_authenticated();
		description_.unlocked_as_owner = security_handler_->unlocked_as_owner();
		description_.permissions = security_handler_->permissions();

		if (!description_.is_authenticated) {
			if (allow_unauthenticated) {
				return {};
			}
			return make_doc_error(
				ErrorCode::NeedsPassword,
				DiagnosticId::NeedsPassword,
				"PDF document is encrypted and requires a password");
		}
	} else {
		description_.is_encrypted = false;
		description_.is_authenticated = true;
		description_.unlocked_as_owner = false;
		description_.encryption_method = CryptFilterMethod::None;
		description_.permissions = PdfPermissions{};
	}

	return {};
}

PdfResult<void> Document::authenticate(std::string_view password) noexcept
{
	try {
		if (!security_handler_.has_value()) {
			return {};
		}
		const bool was_authenticated = description_.is_authenticated;
		RECTO_PDF_RETURN_IF_ERROR(security_handler_->authenticate(password));
		description_.is_authenticated = security_handler_->is_authenticated();
		description_.unlocked_as_owner = security_handler_->unlocked_as_owner();
		description_.permissions = security_handler_->permissions();

		if (!was_authenticated && description_.is_authenticated) {
			RECTO_PDF_RETURN_IF_ERROR(load_document_structure());
		}
		return {};
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while authenticating PDF document");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while authenticating PDF document");
	}
}

PdfResult<PdfValue> Document::resolve_object_with_visited(
	IndirectRef ref,
	std::vector<IndirectRef>& visited_refs) const
{
	IndirectRef current = ref;
	while (true) {
		if (std::find(visited_refs.begin(), visited_refs.end(), current) != visited_refs.end()) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"Cyclic indirect object reference chain detected",
				std::nullopt,
				current.obj_num,
				current.gen_num);
		}
		if (visited_refs.size() >= kMaxIndirectRefDepth) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"Indirect object reference chain exceeds maximum depth of 32",
				std::nullopt,
				current.obj_num,
				current.gen_num);
		}
		visited_refs.push_back(current);

		const XrefEntry* entry = xref_.find(current.obj_num);
		if (entry == nullptr || entry->is_free()) {
			return PdfValue(PdfNull{});
		}

		if (security_handler_.has_value()) {
			if (!security_handler_->is_authenticated()) {
				return make_doc_error(
					ErrorCode::NeedsPassword,
					DiagnosticId::NeedsPassword,
					"Cannot resolve encrypted PDF object before authentication",
					std::nullopt,
					current.obj_num,
					current.gen_num);
			}

			if (entry->is_compressed()) {
				if (current.gen_num != 0u) {
					return PdfValue(PdfNull{});
				}
				const uint32_t stm_obj_num = entry->obj_stm_num;
				const uint32_t stm_index = entry->obj_stm_index;
				const IndirectRef stm_ref{stm_obj_num, 0u};
				if (std::find(visited_refs.begin(), visited_refs.end(), stm_ref) != visited_refs.end()) {
					return make_doc_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedObject,
						"Cyclic /Type /ObjStm reference detected",
						std::nullopt,
						current.obj_num,
						current.gen_num);
				}
				const XrefEntry* stm_entry = xref_.find(stm_obj_num);
				if (stm_entry == nullptr || !stm_entry->is_uncompressed() || stm_entry->generation != 0u) {
					return make_doc_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedObject,
						"Target /Type /ObjStm object is missing, free, or nested",
						std::nullopt,
						stm_obj_num,
						0u);
				}
				RECTO_PDF_TRY(
					IndirectObject raw_stm_obj,
					xref_.read_indirect_object(file_bytes_, stm_ref));
				RECTO_PDF_RETURN_IF_ERROR(
					security_handler_->decrypt_object_values(
						raw_stm_obj.value,
						stm_ref,
						/*is_inside_obj_stm=*/false));
				const PdfStream* obj_stm = raw_stm_obj.value.as_stream();
				if (obj_stm == nullptr ||
					obj_stm->dict.get_name("Type").value_or("") != "ObjStm") {
					return make_doc_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedObject,
						"Compressed object target is not a /Type /ObjStm stream",
						std::nullopt,
						stm_obj_num,
						0u);
				}
				const auto n_opt = obj_stm->dict.get_int("N");
				const auto first_opt = obj_stm->dict.get_int("First");
				if (!n_opt.has_value() || *n_opt <= 0 || *n_opt > kMaxObjStmEntries ||
					!first_opt.has_value() || *first_opt < 0) {
					return make_doc_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedObject,
						"Invalid /N or /First in decrypted /Type /ObjStm stream",
						std::nullopt,
						stm_obj_num,
						0u);
				}
				RECTO_PDF_TRY(
					std::vector<uint8_t> decoded_stm,
					recto::pdf::decode_stream(*obj_stm));
				const uint64_t first_off = static_cast<uint64_t>(*first_opt);
				if (first_off > decoded_stm.size()) {
					return make_doc_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedObject,
						"/Type /ObjStm /First offset exceeds decoded stream length",
						std::nullopt,
						stm_obj_num,
						0u);
				}
				const uint32_t n_count = static_cast<uint32_t>(*n_opt);
				if (stm_index >= n_count) {
					return make_doc_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedObject,
						"Compressed object index exceeds /Type /ObjStm /N entry count",
						std::nullopt,
						current.obj_num,
						current.gen_num);
				}
				std::vector<CachedObjStream::HeaderPair> pairs;
				pairs.reserve(n_count);
				Lexer hdr_lexer(std::span<const uint8_t>(
					decoded_stm.data(),
					static_cast<size_t>(first_off)));
				for (uint32_t i = 0; i < n_count; ++i) {
					RECTO_PDF_TRY(const Token num_tok, hdr_lexer.next_token());
					RECTO_PDF_TRY(const Token off_tok, hdr_lexer.next_token());
					if (num_tok.kind != TokenKind::Integer || num_tok.int_value <= 0 ||
						off_tok.kind != TokenKind::Integer || off_tok.int_value < 0) {
						return make_doc_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedObject,
							"Malformed header pair in decrypted /Type /ObjStm stream",
							std::nullopt,
							stm_obj_num,
							0u);
					}
					const uint64_t rel_off = static_cast<uint64_t>(off_tok.int_value);
					if (rel_off > decoded_stm.size() - first_off ||
						(!pairs.empty() && rel_off < pairs.back().rel_offset)) {
						return make_doc_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedObject,
							"Out-of-bounds relative offset in decrypted /Type /ObjStm stream",
							std::nullopt,
							stm_obj_num,
							0u);
					}
					pairs.push_back(CachedObjStream::HeaderPair{
						static_cast<uint32_t>(num_tok.int_value),
						rel_off});
				}
				if (pairs[stm_index].obj_num != current.obj_num) {
					return make_doc_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedObject,
						"Compressed object number in /Type /ObjStm does not match xref entry",
						std::nullopt,
						current.obj_num,
						current.gen_num);
				}
				const size_t obj_start = static_cast<size_t>(first_off + pairs[stm_index].rel_offset);
				const size_t obj_end = (stm_index + 1u < pairs.size())
					? static_cast<size_t>(first_off + pairs[stm_index + 1u].rel_offset)
					: decoded_stm.size();
				Parser inner_parser(std::span<const uint8_t>(
					decoded_stm.data() + obj_start,
					obj_end - obj_start));
				RECTO_PDF_TRY(PdfValue inner_val, inner_parser.parse_direct_object());
				if (const auto next_ref = inner_val.as_ref()) {
					current = *next_ref;
					continue;
				}
				return inner_val;
			}

			RECTO_PDF_TRY(
				IndirectObject raw_obj,
				xref_.read_indirect_object(file_bytes_, current));
			if (const auto next_ref = raw_obj.value.as_ref()) {
				current = *next_ref;
				continue;
			}
			RECTO_PDF_RETURN_IF_ERROR(
				security_handler_->decrypt_object_values(
					raw_obj.value,
					current,
					/*is_inside_obj_stm=*/false));
			return std::move(raw_obj.value);
		}

		// Unencrypted document:
		RECTO_PDF_TRY(
			IndirectObject raw_obj,
			xref_.read_indirect_object(file_bytes_, current));
		if (const auto next_ref = raw_obj.value.as_ref()) {
			current = *next_ref;
			continue;
		}
		return std::move(raw_obj.value);
	}
}

PdfResult<PdfValue> Document::resolve_object(IndirectRef ref) const noexcept
{
	try {
		std::vector<IndirectRef> visited_refs;
		visited_refs.reserve(kMaxIndirectRefDepth);
		return resolve_object_with_visited(ref, visited_refs);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while resolving PDF indirect object",
			std::nullopt,
			ref.obj_num,
			ref.gen_num);
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while resolving PDF indirect object",
			std::nullopt,
			ref.obj_num,
			ref.gen_num);
	}
}

PdfResult<PdfValue> Document::resolve_value(const PdfValue& value) const noexcept
{
	try {
		if (const auto ref = value.as_ref()) {
			return resolve_object(*ref);
		}
		return value.deep_clone();
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while resolving PDF value");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while resolving PDF value");
	}
}

PdfResult<std::vector<uint8_t>> Document::decode_stream(
	const PdfStream& stream,
	size_t max_decoded_bytes) const noexcept
{
	try {
		if (needs_password()) {
			return make_doc_error(
				ErrorCode::NeedsPassword,
				DiagnosticId::NeedsPassword,
				"Cannot decode PDF stream before document is authenticated");
		}
		const IndirectObjectResolver resolver = [this](IndirectRef ref) -> PdfResult<PdfValue> {
			return this->resolve_object(ref);
		};
		return recto::pdf::decode_stream(stream, max_decoded_bytes, &resolver);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while decoding PDF stream");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while decoding PDF stream");
	}
}

PdfResult<std::vector<uint8_t>> Document::decode_page_contents_from_dict(
	const PdfDict& page_dict) const
{
	const PdfValue* contents_val = page_dict.get("Contents");
	if (contents_val == nullptr || contents_val->is_null()) {
		return std::vector<uint8_t>{};
	}

	if (const auto contents_ref = contents_val->as_ref()) {
		const XrefEntry* entry = xref_.find(contents_ref->obj_num);
		if (entry == nullptr || entry->is_free()) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"/Page /Contents indirect reference points to a missing or free object",
				std::nullopt,
				contents_ref->obj_num,
				contents_ref->gen_num);
		}
	}

	RECTO_PDF_TRY(PdfValue resolved_contents, resolve_value(*contents_val));
	if (resolved_contents.is_null()) {
		return make_doc_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedStream,
			"/Page /Contents resolved to null instead of a stream or array");
	}

	if (const PdfStream* single_stm = resolved_contents.as_stream()) {
		return decode_stream(*single_stm, kMaxDecodedStreamBytes);
	}

	const PdfArray* stm_array = resolved_contents.as_array();
	if (stm_array == nullptr) {
		return make_doc_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedStream,
			"/Page /Contents must be a stream or an array of streams");
	}
	constexpr size_t kMaxPageContentStreams = 4096u;
	if (stm_array->size() > kMaxPageContentStreams) {
		return make_doc_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedStream,
			"/Page /Contents array exceeds maximum stream count");
	}

	std::vector<uint8_t> combined;
	bool has_emitted_stream = false;
	for (const PdfValue& elem : *stm_array) {
		if (elem.is_null()) {
			continue;
		}
		if (const auto elem_ref = elem.as_ref()) {
			const XrefEntry* entry = xref_.find(elem_ref->obj_num);
			if (entry == nullptr || entry->is_free()) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"/Page /Contents array element references a missing or free object",
					std::nullopt,
					elem_ref->obj_num,
					elem_ref->gen_num);
			}
		}
		RECTO_PDF_TRY(PdfValue resolved_elem, resolve_value(elem));
		const PdfStream* stm = resolved_elem.as_stream();
		if (stm == nullptr) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"/Page /Contents array element is not a PDF stream");
		}
		const size_t sep_bytes = has_emitted_stream ? 1u : 0u;
		if (combined.size() + sep_bytes > kMaxDecodedStreamBytes) {
			return make_doc_error(
				ErrorCode::DecompressionLimitExceeded,
				DiagnosticId::DecompressionLimitExceeded,
				"Concatenated /Page /Contents streams exceed 256 MB decoded limit");
		}
		const size_t remaining_budget = kMaxDecodedStreamBytes - combined.size() - sep_bytes;
		RECTO_PDF_TRY(
			std::vector<uint8_t> part,
			decode_stream(*stm, remaining_budget));
		if (has_emitted_stream) {
			// Insert a single whitespace byte ('\n') between adjacent `/Contents`
			// streams so tokens at stream boundaries never fuse and any trailing
			// `%` comment in the preceding stream is terminated cleanly.
			combined.push_back(static_cast<uint8_t>('\n'));
		}
		combined.insert(combined.end(), part.begin(), part.end());
		has_emitted_stream = true;
	}

	return combined;
}

PdfResult<void> Document::load_document_structure()
{
	page_records_.clear();
	page_obj_to_index_.clear();
	named_dests_.clear();
	description_.pages.clear();
	description_.outline.clear();
	description_.fonts.clear();
	description_.metadata = DocumentMetadata{};

	// 1. Resolve `/Root` (`/Type /Catalog`):
	const auto root_ref_opt = xref_.root_ref();
	if (!root_ref_opt.has_value()) {
		return make_doc_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedTrailer,
			"PDF trailer dictionary is missing indirect /Root reference");
	}
	RECTO_PDF_TRY(PdfValue root_val, resolve_object(*root_ref_opt));
	const PdfDict* root_dict = root_val.as_dict();
	if (root_dict == nullptr) {
		return make_doc_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedObject,
			"/Root indirect object is not a PDF dictionary",
			std::nullopt,
			root_ref_opt->obj_num,
			root_ref_opt->gen_num);
	}
	if (const auto root_type = root_dict->get_name("Type");
		root_type.has_value() && *root_type != "Catalog") {
		return make_doc_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedDictionary,
			"/Root dictionary has unexpected /Type (expected /Catalog)",
			std::nullopt,
			root_ref_opt->obj_num,
			root_ref_opt->gen_num);
	}
	catalog_ = *root_dict;

	// Check `/Catalog /Version` override (ISO 32000-1 section 7.5.2 / 7.7.2):
	if (const auto cat_ver = catalog_.get_name("Version")) {
		if (is_valid_pdf_version_string(*cat_ver)) {
			description_.pdf_version = std::string(*cat_ver);
		}
	} else if (const PdfString* cat_ver_str = catalog_.get_string("Version")) {
		if (is_valid_pdf_version_string(cat_ver_str->as_byte_view())) {
			description_.pdf_version = std::string(cat_ver_str->as_byte_view());
		}
	}

	// 2. Traverse `/Pages` tree with `std::unordered_set<int>` cycle guard and
	//    64-level recursion cap:
	const PdfValue* pages_root_val = catalog_.get("Pages");
	if (pages_root_val == nullptr) {
		return make_doc_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedDictionary,
			"/Catalog dictionary is missing required /Pages entry",
			std::nullopt,
			root_ref_opt->obj_num,
			root_ref_opt->gen_num);
	}

	struct InheritedPageAttrs {
		std::optional<PdfRect> media_box{std::nullopt};
		std::optional<PdfRect> crop_box{std::nullopt};
		std::optional<PdfRect> bleed_box{std::nullopt};
		std::optional<PdfRect> trim_box{std::nullopt};
		std::optional<PdfRect> art_box{std::nullopt};
		int32_t rotation{0};
		std::shared_ptr<const PdfDict> resources{nullptr};
	};

	auto extract_box_attr = [this](
								const PdfDict& dict,
								std::string_view key,
								std::optional<PdfRect>& out_box) -> PdfResult<void> {
		const PdfValue* v = dict.get(key);
		if (v == nullptr || v->is_null()) {
			return {};
		}
		RECTO_PDF_TRY(PdfValue resolved, resolve_value(*v));
		if (resolved.is_null()) {
			return {};
		}
		const PdfArray* arr = resolved.as_array();
		if (arr == nullptr || arr->size() != 4u) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"Page box attribute must be a 4-element numeric array");
		}
		std::array<double, 4> coords{};
		for (size_t i = 0; i < 4u; ++i) {
			RECTO_PDF_TRY(PdfValue elem, resolve_value((*arr)[i]));
			const auto num = elem.as_number();
			if (!num.has_value()) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedDictionary,
					"Page box array element is not a finite number");
			}
			coords[i] = *num;
		}
		const PdfRect rect(coords[0], coords[1], coords[2], coords[3]);
		if (rect.is_empty()) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"Page box has zero or negative width/height");
		}
		out_box = rect;
		return {};
	};

	auto extract_rotation_attr = [this](
									 const PdfDict& dict,
									 int32_t& inout_rotation) -> PdfResult<void> {
		const PdfValue* v = dict.get("Rotate");
		if (v == nullptr || v->is_null()) {
			return {};
		}
		RECTO_PDF_TRY(PdfValue resolved, resolve_value(*v));
		if (resolved.is_null()) {
			return {};
		}
		const auto rot_int = resolved.as_int();
		if (!rot_int.has_value() || (*rot_int % 90) != 0) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"/Rotate must be an integer multiple of 90 degrees");
		}
		const int64_t norm = ((*rot_int % 360) + 360) % 360;
		inout_rotation = static_cast<int32_t>(norm);
		return {};
	};

	auto merge_resources_attr = [this](
									const PdfDict& node_dict,
									std::shared_ptr<const PdfDict>& inout_resources) -> PdfResult<void> {
		const PdfValue* res_val = node_dict.get("Resources");
		if (res_val == nullptr || res_val->is_null()) {
			return {};
		}
		RECTO_PDF_TRY(PdfValue resolved_res, resolve_value(*res_val));
		if (resolved_res.is_null()) {
			return {};
		}
		const PdfDict* res_dict = resolved_res.as_dict();
		if (res_dict == nullptr) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"/Resources entry is not a PDF dictionary");
		}

		// Per ISO 32000-1 section 7.7.3.4 ("Inheritance of Page Attributes"),
		// `/Resources` is inherited as a whole dictionary only when omitted on a
		// descendant node; an explicit `/Resources` dictionary on a child node
		// replaces the ancestor's `/Resources` dictionary rather than merging
		// individual categories. Resolve any indirect category sub-dictionaries
		// (`/Font`, `/XObject`, `/ExtGState`, etc.) so `page_resources` holds
		// direct category dictionaries while keeping individual resource entries
		// (`/F1 -> 5 0 R`) intact.
		PdfDict replaced_resources{};
		for (const auto& [cat_key, cat_val] : *res_dict) {
			RECTO_PDF_TRY(PdfValue resolved_cat, resolve_value(cat_val));
			replaced_resources.set(cat_key, std::move(resolved_cat));
		}
		inout_resources = std::make_shared<const PdfDict>(std::move(replaced_resources));
		return {};
	};

	std::unordered_set<int> visited_page_nodes;
	size_t visited_page_tree_nodes = 0u;
	constexpr size_t kMaxPageTreeNodes = kMaxDocumentPages * 2u;

	struct ActivePagesFrame {
		PdfValue kids_val{};
		size_t next_index{0};
		InheritedPageAttrs attrs{};
		size_t depth{0};
	};

	auto process_page_tree_node = [&](const PdfValue& node_val,
									  const InheritedPageAttrs& parent_attrs,
									  size_t depth) -> PdfResult<std::optional<ActivePagesFrame>> {
		if (depth > kMaxPagesTreeDepth) {
			return make_doc_error(
				ErrorCode::RecursionLimitExceeded,
				DiagnosticId::RecursionLimitExceeded,
				"/Pages tree exceeds maximum 64-level depth");
		}
		if (++visited_page_tree_nodes > kMaxPageTreeNodes) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"/Pages tree exceeds maximum supported node budget");
		}

		IndirectRef current_ref{};
		PdfValue resolved_node;
		if (const auto ref = node_val.as_ref()) {
			current_ref = *ref;
			if (!visited_page_nodes.insert(static_cast<int>(ref->obj_num)).second) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedObject,
					"Cyclic /Pages /Kids tree detected",
					std::nullopt,
					ref->obj_num,
					ref->gen_num);
			}
			std::vector<IndirectRef> hop_refs;
			hop_refs.reserve(kMaxIndirectRefDepth);
			RECTO_PDF_TRY(PdfValue val, resolve_object_with_visited(*ref, hop_refs));
			for (size_t h = 1u; h < hop_refs.size(); ++h) {
				if (!visited_page_nodes.insert(static_cast<int>(hop_refs[h].obj_num)).second) {
					return make_doc_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedObject,
						"Cyclic /Pages /Kids tree detected",
						std::nullopt,
						hop_refs[h].obj_num,
						hop_refs[h].gen_num);
				}
			}
			if (!hop_refs.empty()) {
				current_ref = hop_refs.back();
			}
			resolved_node = std::move(val);
		} else {
			RECTO_PDF_TRY(PdfValue cloned_inline, node_val.deep_clone());
			resolved_node = std::move(cloned_inline);
		}

		const PdfDict* node_dict = resolved_node.as_dict();
		if (node_dict == nullptr) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"/Pages tree node is not a PDF dictionary",
				std::nullopt,
				current_ref.obj_num,
				current_ref.gen_num);
		}

		InheritedPageAttrs current_attrs = parent_attrs;
		RECTO_PDF_RETURN_IF_ERROR(extract_box_attr(*node_dict, "MediaBox", current_attrs.media_box));
		RECTO_PDF_RETURN_IF_ERROR(extract_box_attr(*node_dict, "CropBox", current_attrs.crop_box));
		RECTO_PDF_RETURN_IF_ERROR(extract_box_attr(*node_dict, "BleedBox", current_attrs.bleed_box));
		RECTO_PDF_RETURN_IF_ERROR(extract_box_attr(*node_dict, "TrimBox", current_attrs.trim_box));
		RECTO_PDF_RETURN_IF_ERROR(extract_box_attr(*node_dict, "ArtBox", current_attrs.art_box));
		RECTO_PDF_RETURN_IF_ERROR(extract_rotation_attr(*node_dict, current_attrs.rotation));
		RECTO_PDF_RETURN_IF_ERROR(merge_resources_attr(*node_dict, current_attrs.resources));

		const auto node_type = node_dict->get_name("Type").value_or("");
		const bool has_kids = node_dict->contains("Kids");

		if (node_type == "Pages" || (node_type.empty() && has_kids)) {
			const PdfValue* kids_val = node_dict->get("Kids");
			if (kids_val == nullptr) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedDictionary,
					"/Type /Pages node is missing required /Kids array",
					std::nullopt,
					current_ref.obj_num,
					current_ref.gen_num);
			}
			RECTO_PDF_TRY(PdfValue resolved_kids, resolve_value(*kids_val));
			const PdfArray* kids_arr = resolved_kids.as_array();
			if (kids_arr == nullptr) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedDictionary,
					"/Pages /Kids is not a PDF array",
					std::nullopt,
					current_ref.obj_num,
					current_ref.gen_num);
			}
			if (kids_arr->size() > kMaxDocumentPages) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedDictionary,
					"/Pages /Kids array exceeds maximum supported page count",
					std::nullopt,
					current_ref.obj_num,
					current_ref.gen_num);
			}
			return std::optional<ActivePagesFrame>(ActivePagesFrame{
				std::move(resolved_kids),
				0u,
				std::move(current_attrs),
				depth});
		}

		if (node_type == "Page" || (node_type.empty() && !has_kids)) {
			if (page_records_.size() >= kMaxDocumentPages) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedDictionary,
					"Document exceeds maximum supported page count");
			}

			// Validate any additional indirect entries on the `/Page` dictionary
			// (such as custom or extension entries) so malformed or deeply nested
			// indirect objects attached to a page are caught during open.
			for (const auto& [entry_k, entry_v] : *node_dict) {
				const std::string_view k_sv = entry_k.view();
				if (k_sv == "Parent" || k_sv == "Contents" || k_sv == "Resources" ||
					k_sv == "Annots" || k_sv == "Thumb" || k_sv == "B" ||
					k_sv == "PieceInfo" || k_sv == "DPart") {
					continue;
				}
				if (const auto entry_ref = entry_v.as_ref()) {
					RECTO_PDF_TRY(PdfValue resolved_entry, resolve_object(*entry_ref));
					(void)resolved_entry;
				}
			}

			const PdfRect media_box = current_attrs.media_box.value_or(
				PdfRect(0.0, 0.0, 612.0, 792.0));
			const PdfRect crop_box = current_attrs.crop_box.has_value()
				? intersect_or_keep_box(media_box, *current_attrs.crop_box)
				: media_box;

			const uint32_t page_idx = static_cast<uint32_t>(page_records_.size());
			PageDescription page_desc{};
			page_desc.page_index = page_idx;
			page_desc.page_ref = current_ref;
			page_desc.media_box = media_box;
			page_desc.crop_box = crop_box;
			page_desc.bleed_box = current_attrs.bleed_box;
			page_desc.trim_box = current_attrs.trim_box;
			page_desc.art_box = current_attrs.art_box;
			page_desc.rotation = current_attrs.rotation;
			page_desc.rotation_deg = current_attrs.rotation;
			page_desc.width_pt = crop_box.width();
			page_desc.height_pt = crop_box.height();

			ResolvedPageRecord rec{};
			rec.description = page_desc;
			rec.page_dict = *node_dict;
			rec.resources = std::move(current_attrs.resources);

			// Pre-decode page 0's `/Contents` (and validate `/Contents` stream
			// existence on all pages) so truncated streams and deflate bombs on
			// page 0 fail immediately during `Document::open`, while page 0's
			// decoded content stream is ready in O(1) for initial rendering.
			if (page_idx == 0u) {
				RECTO_PDF_TRY(
					std::vector<uint8_t> page0_contents,
					decode_page_contents_from_dict(rec.page_dict));
				rec.cached_contents = std::move(page0_contents);
			} else if (const PdfValue* contents_val = rec.page_dict.get("Contents");
					   contents_val != nullptr && !contents_val->is_null()) {
				if (const auto contents_ref = contents_val->as_ref()) {
					const XrefEntry* c_entry = xref_.find(contents_ref->obj_num);
					if (c_entry == nullptr || c_entry->is_free()) {
						return make_doc_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"/Page /Contents indirect reference points to a missing or free object",
							std::nullopt,
							contents_ref->obj_num,
							contents_ref->gen_num);
					}
				}
			}

			if (current_ref.obj_num != 0u) {
				page_obj_to_index_.emplace(current_ref.obj_num, page_idx);
			}
			description_.pages.push_back(page_desc);
			page_records_.push_back(std::move(rec));
			return std::optional<ActivePagesFrame>(std::nullopt);
		}

		return make_doc_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedDictionary,
			"Invalid /Type in /Pages tree node (expected /Pages or /Page)",
			std::nullopt,
			current_ref.obj_num,
			current_ref.gen_num);
	};

	std::vector<ActivePagesFrame> page_stack;
	page_stack.reserve(kMaxPagesTreeDepth + 2u);

	{
		RECTO_PDF_TRY(
			std::optional<ActivePagesFrame> root_frame,
			process_page_tree_node(*pages_root_val, InheritedPageAttrs{}, 0u));
		if (root_frame.has_value()) {
			page_stack.push_back(std::move(*root_frame));
		}
	}

	while (!page_stack.empty()) {
		const PdfArray* top_kids = page_stack.back().kids_val.as_array();
		if (top_kids == nullptr || page_stack.back().next_index >= top_kids->size()) {
			page_stack.pop_back();
			continue;
		}
		const size_t kid_idx = page_stack.back().next_index++;
		const PdfValue& kid = (*top_kids)[kid_idx];
		if (kid.is_null()) {
			continue;
		}
		const InheritedPageAttrs parent_attrs = page_stack.back().attrs;
		const size_t child_depth = page_stack.back().depth + 1u;
		RECTO_PDF_TRY(
			std::optional<ActivePagesFrame> child_frame,
			process_page_tree_node(kid, parent_attrs, child_depth));
		if (child_frame.has_value()) {
			page_stack.push_back(std::move(*child_frame));
		}
	}

	if (page_records_.empty()) {
		return make_doc_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedDictionary,
			"/Pages tree contains zero /Type /Page leaves");
	}
	description_.page_count = static_cast<uint32_t>(page_records_.size());

	// 3. Extract `/Info` metadata and `/Catalog /Metadata` XMP stream fallback:
	auto extract_info_string = [this](
								   const PdfDict& info_dict,
								   std::string_view key,
								   std::string& out_utf8) -> PdfResult<void> {
		const PdfValue* v = info_dict.get(key);
		if (v == nullptr || v->is_null()) {
			return {};
		}
		RECTO_PDF_TRY(PdfValue resolved, resolve_value(*v));
		if (const PdfString* s = resolved.as_string()) {
			RECTO_PDF_TRY(std::string utf8, pdf_string_to_utf8(s->span()));
			out_utf8 = std::move(utf8);
		} else if (const auto name_v = resolved.as_name()) {
			out_utf8 = std::string(*name_v);
		}
		return {};
	};

	if (const auto info_ref = xref_.info_ref()) {
		auto info_res = resolve_object(*info_ref);
		if (info_res.has_value()) {
			if (const PdfDict* info_dict = info_res->as_dict()) {
				RECTO_PDF_RETURN_IF_ERROR(
					extract_info_string(*info_dict, "Title", description_.metadata.title));
				RECTO_PDF_RETURN_IF_ERROR(
					extract_info_string(*info_dict, "Author", description_.metadata.author));
				RECTO_PDF_RETURN_IF_ERROR(
					extract_info_string(*info_dict, "Subject", description_.metadata.subject));
				RECTO_PDF_RETURN_IF_ERROR(
					extract_info_string(*info_dict, "Keywords", description_.metadata.keywords));
				RECTO_PDF_RETURN_IF_ERROR(
					extract_info_string(*info_dict, "Creator", description_.metadata.creator));
				RECTO_PDF_RETURN_IF_ERROR(
					extract_info_string(*info_dict, "Producer", description_.metadata.producer));
				RECTO_PDF_RETURN_IF_ERROR(
					extract_info_string(*info_dict, "CreationDate", description_.metadata.creation_date));
				RECTO_PDF_RETURN_IF_ERROR(
					extract_info_string(*info_dict, "ModDate", description_.metadata.mod_date));
			}
		}
	} else if (const PdfDict* inline_info = xref_.trailer().get_dict("Info")) {
		RECTO_PDF_RETURN_IF_ERROR(
			extract_info_string(*inline_info, "Title", description_.metadata.title));
		RECTO_PDF_RETURN_IF_ERROR(
			extract_info_string(*inline_info, "Author", description_.metadata.author));
		RECTO_PDF_RETURN_IF_ERROR(
			extract_info_string(*inline_info, "Subject", description_.metadata.subject));
		RECTO_PDF_RETURN_IF_ERROR(
			extract_info_string(*inline_info, "Keywords", description_.metadata.keywords));
		RECTO_PDF_RETURN_IF_ERROR(
			extract_info_string(*inline_info, "Creator", description_.metadata.creator));
		RECTO_PDF_RETURN_IF_ERROR(
			extract_info_string(*inline_info, "Producer", description_.metadata.producer));
		RECTO_PDF_RETURN_IF_ERROR(
			extract_info_string(*inline_info, "CreationDate", description_.metadata.creation_date));
		RECTO_PDF_RETURN_IF_ERROR(
			extract_info_string(*inline_info, "ModDate", description_.metadata.mod_date));
	}

	// XMP `/Catalog /Metadata` fallback:
	if (const PdfValue* meta_val = catalog_.get("Metadata");
		meta_val != nullptr && !meta_val->is_null()) {
		auto meta_obj_res = resolve_value(*meta_val);
		if (meta_obj_res.has_value()) {
			if (const PdfStream* meta_stm = meta_obj_res->as_stream()) {
				auto meta_bytes_res = decode_stream(*meta_stm, 16ULL * 1024ULL * 1024ULL);
				if (meta_bytes_res.has_value()) {
					const std::string_view xmp_xml(
						reinterpret_cast<const char*>(meta_bytes_res->data()),
						meta_bytes_res->size());
					if (description_.metadata.title.empty()) {
						description_.metadata.title = extract_xmp_tag_value(xmp_xml, "dc:title");
					}
					if (description_.metadata.author.empty()) {
						description_.metadata.author = extract_xmp_tag_value(xmp_xml, "dc:creator");
					}
					if (description_.metadata.subject.empty()) {
						description_.metadata.subject =
							extract_xmp_tag_value(xmp_xml, "dc:description");
					}
					if (description_.metadata.keywords.empty()) {
						description_.metadata.keywords =
							extract_xmp_tag_value(xmp_xml, "pdf:Keywords");
						if (description_.metadata.keywords.empty()) {
							description_.metadata.keywords =
								extract_xmp_tag_value(xmp_xml, "dc:subject");
						}
					}
					if (description_.metadata.creator.empty()) {
						description_.metadata.creator =
							extract_xmp_tag_value(xmp_xml, "xmp:CreatorTool");
					}
					if (description_.metadata.producer.empty()) {
						description_.metadata.producer =
							extract_xmp_tag_value(xmp_xml, "pdf:Producer");
					}
					if (description_.metadata.creation_date.empty()) {
						description_.metadata.creation_date =
							extract_xmp_tag_value(xmp_xml, "xmp:CreateDate");
					}
					if (description_.metadata.mod_date.empty()) {
						description_.metadata.mod_date =
							extract_xmp_tag_value(xmp_xml, "xmp:ModifyDate");
					}
				}
			}
		}
	}

	if (!description_.metadata.creation_date.empty()) {
		description_.metadata.parsed_creation_date =
			parse_pdf_date(description_.metadata.creation_date);
	}
	if (!description_.metadata.mod_date.empty()) {
		description_.metadata.parsed_mod_date =
			parse_pdf_date(description_.metadata.mod_date);
	}

	// 4. Collect named destinations from `/Catalog /Dests` and `/Catalog /Names /Dests`:
	auto parse_dest_array = [this](const PdfArray& arr) -> ParsedDest {
		ParsedDest out{};
		if (arr.empty()) {
			return out;
		}
		if (const auto page_ref = arr[0].as_ref()) {
			const auto it = page_obj_to_index_.find(page_ref->obj_num);
			if (it == page_obj_to_index_.end()) {
				return out;
			}
			out.page_index = static_cast<int32_t>(it->second);
		} else {
			auto p0_res = resolve_value(arr[0]);
			if (!p0_res.has_value()) {
				return out;
			}
			if (const auto idx_int = p0_res->as_int()) {
				if (*idx_int >= 0 &&
					static_cast<size_t>(*idx_int) < page_records_.size()) {
					out.page_index = static_cast<int32_t>(*idx_int);
				} else {
					return out;
				}
			} else {
				return out;
			}
		}

		const PdfRect& page_box = page_records_[static_cast<size_t>(out.page_index)].description.crop_box;
		out.y_pt = page_box.y_max;
		out.valid = true;

		if (arr.size() < 2u) {
			return out;
		}
		auto fit_res = resolve_value(arr[1]);
		if (!fit_res.has_value()) {
			return out;
		}
		const auto fit_name = fit_res->as_name();
		if (!fit_name.has_value()) {
			return out;
		}
		out.fit_mode = std::string(*fit_name);

		auto get_num_arg = [&](size_t idx) -> std::optional<double> {
			if (idx >= arr.size()) {
				return std::nullopt;
			}
			auto v_res = resolve_value(arr[idx]);
			if (!v_res.has_value()) {
				return std::nullopt;
			}
			return v_res->as_number();
		};

		if (*fit_name == "XYZ") {
			if (const auto left = get_num_arg(2u)) {
				out.x_pt = *left;
			}
			if (const auto top = get_num_arg(3u)) {
				out.y_pt = *top;
			}
			if (const auto zm = get_num_arg(4u)) {
				out.zoom = *zm;
			}
		} else if (*fit_name == "FitH" || *fit_name == "FitBH") {
			if (const auto top = get_num_arg(2u)) {
				out.y_pt = *top;
			}
		} else if (*fit_name == "FitV" || *fit_name == "FitBV") {
			if (const auto left = get_num_arg(2u)) {
				out.x_pt = *left;
			}
		} else if (*fit_name == "FitR") {
			if (const auto left = get_num_arg(2u)) {
				out.x_pt = *left;
			}
			if (const auto top = get_num_arg(5u)) {
				out.y_pt = *top;
			}
		}
		return out;
	};

	std::unordered_map<uint32_t, ParsedDest> parsed_dest_obj_cache;
	auto parse_direct_dest_target = [&](const PdfValue& dval) -> ParsedDest {
		if (const PdfArray* arr = dval.as_array()) {
			return parse_dest_array(*arr);
		}
		if (const PdfDict* ddict = dval.as_dict()) {
			if (const PdfValue* inner_d = ddict->get("D")) {
				if (const auto inner_ref = inner_d->as_ref()) {
					const auto cache_it = parsed_dest_obj_cache.find(inner_ref->obj_num);
					if (cache_it != parsed_dest_obj_cache.end()) {
						return cache_it->second;
					}
					std::vector<IndirectRef> hop_refs;
					hop_refs.reserve(kMaxIndirectRefDepth);
					auto inner_res = resolve_object_with_visited(*inner_ref, hop_refs);
					ParsedDest pd{};
					if (inner_res.has_value()) {
						if (const PdfArray* arr = inner_res->as_array()) {
							pd = parse_dest_array(*arr);
						}
					}
					for (const IndirectRef& hr : hop_refs) {
						parsed_dest_obj_cache.emplace(hr.obj_num, pd);
					}
					return pd;
				}
				auto inner_res = resolve_value(*inner_d);
				if (inner_res.has_value()) {
					if (const PdfArray* arr = inner_res->as_array()) {
						return parse_dest_array(*arr);
					}
				}
			}
		}
		return ParsedDest{};
	};

	auto parse_dest_target_value = [&](const PdfValue& raw_v) -> ParsedDest {
		if (const auto ref = raw_v.as_ref()) {
			const auto cache_it = parsed_dest_obj_cache.find(ref->obj_num);
			if (cache_it != parsed_dest_obj_cache.end()) {
				return cache_it->second;
			}
			std::vector<IndirectRef> hop_refs;
			hop_refs.reserve(kMaxIndirectRefDepth);
			auto resolved_res = resolve_object_with_visited(*ref, hop_refs);
			ParsedDest pd{};
			if (resolved_res.has_value()) {
				pd = parse_direct_dest_target(*resolved_res);
			}
			for (const IndirectRef& hr : hop_refs) {
				parsed_dest_obj_cache.emplace(hr.obj_num, pd);
			}
			return pd;
		}
		auto resolved_res = resolve_value(raw_v);
		if (!resolved_res.has_value()) {
			return ParsedDest{};
		}
		return parse_direct_dest_target(*resolved_res);
	};

	constexpr size_t kMaxDestNameBytes = 4096u;
	constexpr size_t kMaxTotalNamedDestBytes = 16u * 1024u * 1024u;
	size_t total_named_dest_bytes = 0u;
	auto record_named_dest = [&](std::string&& name, const PdfValue& target_val) -> PdfResult<void> {
		if (name.empty()) {
			return {};
		}
		if (name.size() > kMaxDestNameBytes) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"Named destination key exceeds maximum length");
		}
		const size_t entry_bytes = name.size() + sizeof(ParsedDest);
		if (named_dests_.size() >= kMaxNameTreeEntries ||
			total_named_dest_bytes + entry_bytes > kMaxTotalNamedDestBytes) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"Named destinations exceed maximum entry or memory budget");
		}
		ParsedDest pd = parse_dest_target_value(target_val);
		if (!pd.valid) {
			return {};
		}
		const auto [it, inserted] = named_dests_.insert_or_assign(std::move(name), std::move(pd));
		if (inserted) {
			total_named_dest_bytes += entry_bytes;
		}
		return {};
	};

	if (const PdfValue* dests_dict_val = catalog_.get("Dests");
		dests_dict_val != nullptr && !dests_dict_val->is_null()) {
		auto dests_res = resolve_value(*dests_dict_val);
		if (dests_res.has_value()) {
			if (const PdfDict* dests_dict = dests_res->as_dict()) {
				for (const auto& [k, v] : *dests_dict) {
					RECTO_PDF_RETURN_IF_ERROR(record_named_dest(std::string(k.view()), v));
				}
			}
		}
	}

	if (const PdfValue* names_val = catalog_.get("Names");
		names_val != nullptr && !names_val->is_null()) {
		auto names_res = resolve_value(*names_val);
		if (names_res.has_value()) {
			if (const PdfDict* names_dict = names_res->as_dict()) {
				if (const PdfValue* dests_tree_val = names_dict->get("Dests");
					dests_tree_val != nullptr && !dests_tree_val->is_null()) {
					std::unordered_set<uint32_t> visited_name_nodes;
					size_t total_name_nodes = 0u;
					constexpr size_t kMaxNameTreeNodes = 4096u;
					auto walk_name_tree = [&](auto& self,
											  const PdfValue& node_v,
											  size_t depth) -> PdfResult<void> {
						if (depth > kMaxNameTreeDepth) {
							return make_doc_error(
								ErrorCode::RecursionLimitExceeded,
								DiagnosticId::RecursionLimitExceeded,
								"/Names /Dests tree exceeds maximum depth");
						}
						if (++total_name_nodes > kMaxNameTreeNodes) {
							return make_doc_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedDictionary,
								"/Names /Dests tree exceeds maximum node budget");
						}
						PdfValue resolved_node_val;
						if (const auto ref = node_v.as_ref()) {
							if (!visited_name_nodes.insert(ref->obj_num).second) {
								return {};
							}
							std::vector<IndirectRef> hop_refs;
							hop_refs.reserve(kMaxIndirectRefDepth);
							auto resolved_res = resolve_object_with_visited(*ref, hop_refs);
							if (!resolved_res.has_value()) {
								return {};
							}
							for (size_t h = 1u; h < hop_refs.size(); ++h) {
								if (!visited_name_nodes.insert(hop_refs[h].obj_num).second) {
									return {};
								}
							}
							resolved_node_val = std::move(*resolved_res);
						} else {
							auto resolved_res = resolve_value(node_v);
							if (!resolved_res.has_value()) {
								return {};
							}
							resolved_node_val = std::move(*resolved_res);
						}
						const PdfDict* tree_dict = resolved_node_val.as_dict();
						if (tree_dict == nullptr) {
							return {};
						}

						if (const PdfValue* n_arr_v = tree_dict->get("Names")) {
							auto n_arr_res = resolve_value(*n_arr_v);
							if (n_arr_res.has_value()) {
								if (const PdfArray* n_arr = n_arr_res->as_array()) {
									for (size_t i = 0; i + 1u < n_arr->size(); i += 2u) {
										auto k_res = resolve_value((*n_arr)[i]);
										if (!k_res.has_value()) {
											continue;
										}
										std::string dest_name;
										if (const PdfString* ks = k_res->as_string()) {
											auto utf8_res = pdf_string_to_utf8(ks->span());
											dest_name = utf8_res.has_value()
												? std::move(*utf8_res)
												: std::string(ks->as_byte_view());
										} else if (const auto kn = k_res->as_name()) {
											dest_name = std::string(*kn);
										}
										if (!dest_name.empty()) {
											RECTO_PDF_RETURN_IF_ERROR(
												record_named_dest(std::move(dest_name), (*n_arr)[i + 1u]));
										}
									}
								}
							}
						}

						if (const PdfValue* kids_v = tree_dict->get("Kids")) {
							auto kids_res = resolve_value(*kids_v);
							if (kids_res.has_value()) {
								if (const PdfArray* kids_arr = kids_res->as_array()) {
									for (const PdfValue& kid : *kids_arr) {
										RECTO_PDF_RETURN_IF_ERROR(self(self, kid, depth + 1u));
									}
								}
							}
						}
						return {};
					};

					RECTO_PDF_RETURN_IF_ERROR(
						walk_name_tree(walk_name_tree, *dests_tree_val, 0u));
				}
			}
		}
	}

	auto resolve_dest_value = [&](const PdfValue& raw_dest) -> ParsedDest {
		if (const auto ref = raw_dest.as_ref()) {
			const auto cache_it = parsed_dest_obj_cache.find(ref->obj_num);
			if (cache_it != parsed_dest_obj_cache.end()) {
				return cache_it->second;
			}
		}
		auto resolved_res = resolve_value(raw_dest);
		if (!resolved_res.has_value()) {
			return ParsedDest{};
		}
		const PdfValue& dval = *resolved_res;
		if (dval.is_array() || dval.is_dict()) {
			return parse_direct_dest_target(dval);
		}

		std::string name_key;
		if (const auto nm = dval.as_name()) {
			name_key = std::string(*nm);
		} else if (const PdfString* s = dval.as_string()) {
			auto utf8_res = pdf_string_to_utf8(s->span());
			name_key = utf8_res.has_value()
				? std::move(*utf8_res)
				: std::string(s->as_byte_view());
		}
		if (!name_key.empty()) {
			const auto it = named_dests_.find(name_key);
			if (it != named_dests_.end()) {
				return it->second;
			}
		}
		return ParsedDest{};
	};

	// 5. Resolve `/Catalog /Outlines` tree with cycle guard on `/First`/`/Next`:
	if (const PdfValue* outlines_val = catalog_.get("Outlines");
		outlines_val != nullptr && !outlines_val->is_null()) {
		auto outlines_res = resolve_value(*outlines_val);
		if (outlines_res.has_value()) {
			if (const PdfDict* outlines_root = outlines_res->as_dict()) {
				std::unordered_set<uint32_t> visited_outline_objs;
				if (const auto root_out_ref = outlines_val->as_ref()) {
					visited_outline_objs.insert(root_out_ref->obj_num);
				}
				size_t total_outline_nodes = 0;

				auto build_outline_siblings = [&](auto& self,
												  const PdfValue* first_val,
												  std::vector<OutlineNode>& out_siblings,
												  size_t depth) -> PdfResult<void> {
					if (first_val == nullptr || first_val->is_null() ||
						depth > kMaxOutlineDepth ||
						total_outline_nodes >= kMaxOutlineNodes) {
						return {};
					}

					PdfValue current_ptr;
					RECTO_PDF_TRY(PdfValue cloned_first, first_val->deep_clone());
					current_ptr = std::move(cloned_first);

					while (!current_ptr.is_null() && total_outline_nodes < kMaxOutlineNodes) {
						PdfValue item_val;
						if (const auto item_ref = current_ptr.as_ref()) {
							if (!visited_outline_objs.insert(item_ref->obj_num).second) {
								record_diagnostic(
									ErrorCode::DamagedFormat,
									DiagnosticId::DamagedDictionary,
									std::nullopt,
									item_ref->obj_num,
									item_ref->gen_num);
								break;
							}
							std::vector<IndirectRef> hop_refs;
							hop_refs.reserve(kMaxIndirectRefDepth);
							auto item_res = resolve_object_with_visited(*item_ref, hop_refs);
							if (!item_res.has_value()) {
								break;
							}
							bool cycle_in_hops = false;
							for (size_t h = 1u; h < hop_refs.size(); ++h) {
								if (!visited_outline_objs.insert(hop_refs[h].obj_num).second) {
									record_diagnostic(
										ErrorCode::DamagedFormat,
										DiagnosticId::DamagedDictionary,
										std::nullopt,
										hop_refs[h].obj_num,
										hop_refs[h].gen_num);
									cycle_in_hops = true;
									break;
								}
							}
							if (cycle_in_hops) {
								break;
							}
							item_val = std::move(*item_res);
						} else {
							auto item_res = resolve_value(current_ptr);
							if (!item_res.has_value()) {
								break;
							}
							item_val = std::move(*item_res);
						}
						const PdfDict* item_dict = item_val.as_dict();
						if (item_dict == nullptr) {
							break;
						}

						++total_outline_nodes;
						OutlineNode node{};
						if (const PdfValue* title_v = item_dict->get("Title")) {
							auto title_res = resolve_value(*title_v);
							if (title_res.has_value()) {
								if (const PdfString* ts = title_res->as_string()) {
									auto utf8_res = pdf_string_to_utf8(ts->span());
									if (utf8_res.has_value()) {
										node.title = std::move(*utf8_res);
									}
								}
							}
						}

						if (const PdfValue* count_v = item_dict->get("Count")) {
							auto count_res = resolve_value(*count_v);
							if (count_res.has_value()) {
								if (const auto c_int = count_res->as_int()) {
									node.is_open = (*c_int >= 0);
								}
							}
						}

						if (const PdfValue* dest_v = item_dict->get("Dest")) {
							const ParsedDest pd = resolve_dest_value(*dest_v);
							if (pd.valid) {
								node.page_index = pd.page_index;
								node.dest_x_pt = pd.x_pt;
								node.dest_y_pt = pd.y_pt;
								node.zoom = pd.zoom;
								node.fit_mode = pd.fit_mode;
								node.has_destination = true;
							}
						} else if (const PdfValue* act_v = item_dict->get("A")) {
							auto act_res = resolve_value(*act_v);
							if (act_res.has_value()) {
								if (const PdfDict* act_dict = act_res->as_dict()) {
									const auto s_name = act_dict->get_name("S").value_or("");
									if (s_name == "GoTo") {
										if (const PdfValue* d_v = act_dict->get("D")) {
											const ParsedDest pd = resolve_dest_value(*d_v);
											if (pd.valid) {
												node.page_index = pd.page_index;
												node.dest_x_pt = pd.x_pt;
												node.dest_y_pt = pd.y_pt;
												node.zoom = pd.zoom;
												node.fit_mode = pd.fit_mode;
												node.has_destination = true;
											}
										}
									} else if (s_name == "URI") {
										if (const PdfValue* uri_v = act_dict->get("URI")) {
											auto uri_res = resolve_value(*uri_v);
											if (uri_res.has_value()) {
												if (const PdfString* us = uri_res->as_string()) {
													auto u_utf8 = pdf_string_to_utf8(us->span());
													if (u_utf8.has_value() &&
														is_safe_external_uri(*u_utf8)) {
														node.uri = std::move(*u_utf8);
													}
												}
											}
										}
									}
								}
							}
						}

						if (const PdfValue* child_first = item_dict->get("First")) {
							RECTO_PDF_RETURN_IF_ERROR(
								self(self, child_first, node.children, depth + 1u));
						}

						const PdfValue* next_v = item_dict->get("Next");
						PdfValue next_ptr{};
						if (next_v != nullptr && !next_v->is_null()) {
							RECTO_PDF_TRY(PdfValue cloned_next, next_v->deep_clone());
							next_ptr = std::move(cloned_next);
						}
						out_siblings.push_back(std::move(node));
						current_ptr = std::move(next_ptr);
					}
					return {};
				};

				RECTO_PDF_RETURN_IF_ERROR(
					build_outline_siblings(
						build_outline_siblings,
						outlines_root->get("First"),
						description_.outline,
						0u));
			}
		}
	}

	// 6. Extract per-page `/Subtype /Link` annotations:
	auto record_refused_action_diagnostic = [](std::string_view s_name) noexcept {
		if (s_name == "Launch") {
			record_diagnostic(ErrorCode::DamagedFormat, DiagnosticId::BlockedLaunchAction);
		} else if (s_name == "JavaScript") {
			record_diagnostic(ErrorCode::DamagedFormat, DiagnosticId::BlockedJavaScriptAction);
		} else if (s_name == "SubmitForm" || s_name == "ResetForm") {
			record_diagnostic(ErrorCode::DamagedFormat, DiagnosticId::BlockedSubmitFormAction);
		} else if (s_name == "ImportData") {
			record_diagnostic(ErrorCode::DamagedFormat, DiagnosticId::BlockedImportDataAction);
		}
	};

	auto action_chain_contains_refused = [this, &record_refused_action_diagnostic](
											 const PdfDict& root_action,
											 std::optional<uint32_t> root_obj_num = std::nullopt) -> bool {
		constexpr size_t kMaxActionChainDepth = 64u;
		constexpr size_t kMaxActionChainNodes = 256u;

		struct ActionFrame {
			PdfValue value{};
			size_t depth{0};
			bool from_array{false};
		};

		std::unordered_set<uint32_t> visited_actions;
		if (root_obj_num.has_value()) {
			visited_actions.insert(*root_obj_num);
		}
		std::vector<ActionFrame> stack;
		size_t budget_used = 1u;
		size_t inspected_nodes = 0u;

		auto is_single_action_unsafe = [&](const PdfDict& act_dict) -> bool {
			const auto s_name = act_dict.get_name("S").value_or("");
			if (s_name.empty()) {
				return true;
			}
			if (is_refused_action_name(s_name)) {
				record_refused_action_diagnostic(s_name);
				return true;
			}
			if (s_name == "URI") {
				const PdfValue* uri_v = act_dict.get("URI");
				if (uri_v == nullptr || uri_v->is_null()) {
					return true;
				}
				auto uri_res = resolve_value(*uri_v);
				if (!uri_res.has_value()) {
					return true;
				}
				const PdfString* us = uri_res->as_string();
				if (us == nullptr) {
					return true;
				}
				auto u_utf8 = pdf_string_to_utf8(us->span());
				if (!u_utf8.has_value() || !is_safe_external_uri(*u_utf8)) {
					return true;
				}
				return false;
			}
			if (s_name == "GoTo" || s_name == "GoToR" || s_name == "GoToE" || s_name == "Named") {
				return false;
			}
			// Any unrecognized action type in a chain is treated as refused/inert.
			return true;
		};

		auto enqueue_next_array = [&](const PdfArray& arr, size_t elem_depth) -> bool {
			if (elem_depth > kMaxActionChainDepth) {
				return false;
			}
			if (arr.size() > kMaxActionChainNodes ||
				budget_used + arr.size() > kMaxActionChainNodes) {
				return false;
			}
			for (size_t i = arr.size(); i > 0u; --i) {
				const PdfValue& elem = arr[i - 1u];
				if (elem.is_null()) {
					continue;
				}
				if (budget_used >= kMaxActionChainNodes) {
					return false;
				}
				auto cloned_elem = elem.deep_clone();
				if (!cloned_elem.has_value()) {
					return false;
				}
				++budget_used;
				stack.push_back(ActionFrame{std::move(*cloned_elem), elem_depth, /*from_array=*/true});
			}
			return true;
		};

		auto enqueue_next_value = [&](const PdfValue& next_item, size_t child_depth) -> bool {
			if (next_item.is_null()) {
				return true;
			}
			if (child_depth > kMaxActionChainDepth) {
				// Fail closed when action chain exceeds maximum depth.
				return false;
			}
			if (const PdfArray* next_arr = next_item.as_array()) {
				return enqueue_next_array(*next_arr, child_depth);
			}
			if (budget_used >= kMaxActionChainNodes) {
				return false;
			}
			auto cloned_next = next_item.deep_clone();
			if (!cloned_next.has_value()) {
				return false;
			}
			++budget_used;
			stack.push_back(ActionFrame{std::move(*cloned_next), child_depth, /*from_array=*/false});
			return true;
		};

		// Inspect root_action directly before resolving any /Next children:
		++inspected_nodes;
		if (is_single_action_unsafe(root_action)) {
			return true;
		}
		if (const PdfValue* root_next = root_action.get("Next");
			root_next != nullptr && !root_next->is_null()) {
			if (!enqueue_next_value(*root_next, 1u)) {
				return true;
			}
		}

		while (!stack.empty()) {
			ActionFrame frame = std::move(stack.back());
			stack.pop_back();

			if (frame.depth > kMaxActionChainDepth ||
				++inspected_nodes > kMaxActionChainNodes) {
				// Fail closed if the action graph exceeds depth or node budget.
				return true;
			}

			PdfValue cur_val = std::move(frame.value);
			bool already_visited = false;
			size_t ref_hops = 0u;
			while (const auto ref = cur_val.as_ref()) {
				if (!visited_actions.insert(ref->obj_num).second) {
					already_visited = true;
					break;
				}
				if (++ref_hops > kMaxIndirectRefDepth) {
					return true;
				}
				const XrefEntry* entry = xref_.find(ref->obj_num);
				if (entry == nullptr || entry->is_free()) {
					return true;
				}
				if (entry->is_uncompressed()) {
					auto raw_res = xref_.read_indirect_object(file_bytes_, *ref);
					if (!raw_res.has_value()) {
						return true;
					}
					if (raw_res->value.is_ref()) {
						cur_val = std::move(raw_res->value);
						continue;
					}
					if (security_handler_.has_value()) {
						if (!security_handler_->is_authenticated() ||
							!security_handler_->decrypt_object_values(
								raw_res->value,
								*ref,
								/*is_inside_obj_stm=*/false).has_value()) {
							return true;
						}
					}
					cur_val = std::move(raw_res->value);
					break;
				}
				auto res = resolve_object(*ref);
				if (!res.has_value()) {
					return true;
				}
				cur_val = std::move(*res);
				break;
			}
			if (already_visited) {
				continue;
			}

			if (const PdfArray* arr = cur_val.as_array()) {
				const size_t elem_depth = frame.from_array ? (frame.depth + 1u) : frame.depth;
				if (!enqueue_next_array(*arr, elem_depth)) {
					return true;
				}
				continue;
			}

			const PdfDict* act_dict = cur_val.as_dict();
			if (act_dict == nullptr) {
				return true;
			}

			if (is_single_action_unsafe(*act_dict)) {
				return true;
			}

			if (const PdfValue* next_v = act_dict->get("Next");
				next_v != nullptr && !next_v->is_null()) {
				if (!enqueue_next_value(*next_v, frame.depth + 1u)) {
					return true;
				}
			}
		}
		return false;
	};

	constexpr size_t kMaxAnnotsPerPage = 65'536u;
	for (ResolvedPageRecord& page_rec : page_records_) {
		const PdfValue* annots_val = page_rec.page_dict.get("Annots");
		if (annots_val == nullptr || annots_val->is_null()) {
			continue;
		}
		auto annots_res = resolve_value(*annots_val);
		if (!annots_res.has_value()) {
			continue;
		}
		const PdfArray* annots_arr = annots_res->as_array();
		if (annots_arr == nullptr) {
			continue;
		}
		if (annots_arr->size() > kMaxAnnotsPerPage) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"/Page /Annots array exceeds maximum annotation count");
		}

		std::unordered_set<uint32_t> visited_page_annots;
		for (const PdfValue& annot_elem : *annots_arr) {
			if (annot_elem.is_null()) {
				continue;
			}
			PdfValue annot_val;
			if (const auto annot_ref = annot_elem.as_ref()) {
				if (!visited_page_annots.insert(annot_ref->obj_num).second) {
					continue;
				}
				std::vector<IndirectRef> hop_refs;
				hop_refs.reserve(kMaxIndirectRefDepth);
				auto annot_res = resolve_object_with_visited(*annot_ref, hop_refs);
				if (!annot_res.has_value()) {
					continue;
				}
				bool duplicate_alias = false;
				for (size_t h = 1u; h < hop_refs.size(); ++h) {
					if (!visited_page_annots.insert(hop_refs[h].obj_num).second) {
						duplicate_alias = true;
						break;
					}
				}
				if (duplicate_alias) {
					continue;
				}
				annot_val = std::move(*annot_res);
			} else {
				auto annot_res = resolve_value(annot_elem);
				if (!annot_res.has_value()) {
					continue;
				}
				annot_val = std::move(*annot_res);
			}
			const PdfDict* annot_dict = annot_val.as_dict();
			if (annot_dict == nullptr) {
				continue;
			}
			const auto subtype = annot_dict->get_name("Subtype").value_or("");
			if (subtype != "Link") {
				continue;
			}

			std::optional<PdfRect> rect_opt;
			if (extract_box_attr(*annot_dict, "Rect", rect_opt).has_value() &&
				rect_opt.has_value()) {
				// Use parsed `/Rect`
			} else if (const auto direct_rect = annot_dict->get_rect("Rect")) {
				rect_opt = *direct_rect;
			} else {
				continue;
			}

			PageLink link{};
			link.rect_pt = *rect_opt;
			link.kind = LinkKind::RefusedAction;
			link.is_inert = true;

			// Check `/AA` (Additional-Actions) for any refused triggers:
			bool aa_has_refused = false;
			if (const PdfValue* aa_val = annot_dict->get("AA")) {
				auto aa_res = resolve_value(*aa_val);
				if (aa_res.has_value()) {
					if (const PdfDict* aa_dict = aa_res->as_dict()) {
						for (const auto& [aa_k, aa_v] : *aa_dict) {
							if (aa_v.is_null()) {
								continue;
							}
							const auto sub_ref = aa_v.as_ref();
							auto sub_res = resolve_value(aa_v);
							if (!sub_res.has_value() || sub_res->as_dict() == nullptr) {
								aa_has_refused = true;
								link.action_type = "AdditionalAction";
								break;
							}
							const PdfDict* sub_dict = sub_res->as_dict();
							if (action_chain_contains_refused(
									*sub_dict,
									sub_ref ? std::optional<uint32_t>(sub_ref->obj_num) : std::nullopt)) {
								aa_has_refused = true;
								link.action_type = std::string(
									sub_dict->get_name("S").value_or("AdditionalAction"));
								break;
							}
						}
					}
				}
			}

			if (aa_has_refused) {
				link.kind = LinkKind::RefusedAction;
				link.is_inert = true;
				page_rec.links.push_back(std::move(link));
				continue;
			}

			if (const PdfValue* act_val = annot_dict->get("A");
				act_val != nullptr && !act_val->is_null()) {
				const auto act_ref = act_val->as_ref();
				auto act_res = resolve_value(*act_val);
				if (act_res.has_value()) {
					if (const PdfDict* act_dict = act_res->as_dict()) {
						std::string s_type;
						if (const auto s_name = act_dict->get_name("S")) {
							s_type = std::string(*s_name);
						}
						link.action_type = s_type;

						if (action_chain_contains_refused(
								*act_dict,
								act_ref ? std::optional<uint32_t>(act_ref->obj_num) : std::nullopt)) {
							link.kind = LinkKind::RefusedAction;
							link.is_inert = true;
						} else if (s_type == "GoTo") {
							if (const PdfValue* d_val = act_dict->get("D")) {
								const ParsedDest pd = resolve_dest_value(*d_val);
								if (pd.valid) {
									link.kind = LinkKind::Internal;
									link.is_inert = false;
									link.page_index = pd.page_index;
									link.x_pt = pd.x_pt;
									link.y_pt = pd.y_pt;
									link.dest_y_pt = pd.y_pt;
									link.zoom = pd.zoom;
								}
							}
						} else if (s_type == "URI") {
							if (const PdfValue* uri_val = act_dict->get("URI")) {
								auto uri_res = resolve_value(*uri_val);
								if (uri_res.has_value()) {
									if (const PdfString* us = uri_res->as_string()) {
										auto u_utf8 = pdf_string_to_utf8(us->span());
										if (u_utf8.has_value()) {
											link.uri = std::string(trim_ascii_whitespace(*u_utf8));
											if (is_safe_external_uri(link.uri)) {
												link.kind = LinkKind::ExternalUri;
												link.is_inert = false;
											} else {
												link.kind = LinkKind::RefusedAction;
												link.is_inert = true;
											}
										}
									}
								}
							}
						} else if (s_type == "GoToR" || s_type == "GoToE") {
							link.kind = LinkKind::RemoteGoTo;
							link.is_inert = false;
							if (const PdfValue* f_val = act_dict->get("F")) {
								auto f_res = resolve_value(*f_val);
								if (f_res.has_value()) {
									if (const PdfString* fs = f_res->as_string()) {
										auto f_utf8 = pdf_string_to_utf8(fs->span());
										if (f_utf8.has_value()) {
											link.uri = std::move(*f_utf8);
										}
									} else if (const PdfDict* fdict = f_res->as_dict()) {
										const PdfString* uf = fdict->get_string("UF");
										if (uf == nullptr) {
											uf = fdict->get_string("F");
										}
										if (uf != nullptr) {
											auto f_utf8 = pdf_string_to_utf8(uf->span());
											if (f_utf8.has_value()) {
												link.uri = std::move(*f_utf8);
											}
										}
									}
								}
							}
							if (const PdfValue* d_val = act_dict->get("D")) {
								auto d_res = resolve_value(*d_val);
								if (d_res.has_value()) {
									if (const PdfArray* d_arr = d_res->as_array();
										d_arr != nullptr && !d_arr->empty()) {
										if (const auto p_int = (*d_arr)[0].as_int();
											p_int.has_value() && *p_int >= 0) {
											link.page_index = static_cast<int32_t>(*p_int);
										}
									}
								}
							}
						} else if (s_type == "Named") {
							if (const auto n_name = act_dict->get_name("N")) {
								link.kind = LinkKind::NamedAction;
								link.is_inert = false;
								link.uri = std::string(*n_name);
							}
						} else {
							// `/Launch`, `/JavaScript`, or any other unknown action is inert.
							link.kind = LinkKind::RefusedAction;
							link.is_inert = true;
						}
					}
				}
			} else if (const PdfValue* dest_val = annot_dict->get("Dest");
					   dest_val != nullptr && !dest_val->is_null()) {
				const ParsedDest pd = resolve_dest_value(*dest_val);
				if (pd.valid) {
					link.kind = LinkKind::Internal;
					link.is_inert = false;
					link.action_type = "Dest";
					link.page_index = pd.page_index;
					link.x_pt = pd.x_pt;
					link.y_pt = pd.y_pt;
					link.dest_y_pt = pd.y_pt;
					link.zoom = pd.zoom;
				}
			}

			page_rec.links.push_back(std::move(link));
		}
	}

	// 7. Enumerate and deduplicate `/Font` descriptors across pages, Form
	//    XObjects, and `/Catalog /AcroForm /DR`:
	std::unordered_set<uint32_t> visited_font_objs;
	std::unordered_set<uint32_t> visited_xobj_objs;

	auto has_embedded_font_file = [this](const PdfDict& font_or_cid_dict) -> bool {
		if (font_or_cid_dict.contains("FontFile") ||
			font_or_cid_dict.contains("FontFile2") ||
			font_or_cid_dict.contains("FontFile3")) {
			return true;
		}
		const PdfValue* fd_val = font_or_cid_dict.get("FontDescriptor");
		if (fd_val == nullptr || fd_val->is_null()) {
			return false;
		}
		auto fd_res = resolve_value(*fd_val);
		if (!fd_res.has_value()) {
			return false;
		}
		const PdfDict* fd_dict = fd_res->as_dict();
		if (fd_dict == nullptr) {
			return false;
		}
		for (std::string_view key : {"FontFile", "FontFile2", "FontFile3"}) {
			if (const PdfValue* ff = fd_dict->get(key); ff != nullptr && !ff->is_null()) {
				return true;
			}
		}
		return false;
	};

	auto inspect_font_dict = [&](const PdfDict& font_dict, std::string_view fallback_key) {
		FontSummary summary{};
		summary.subtype = std::string(font_dict.get_name("Subtype").value_or(""));

		if (const auto bf_name = font_dict.get_name("BaseFont")) {
			summary.name = std::string(*bf_name);
		} else if (const PdfString* bf_str = font_dict.get_string("BaseFont")) {
			auto utf8_res = pdf_string_to_utf8(bf_str->span());
			summary.name = utf8_res.has_value()
				? std::move(*utf8_res)
				: std::string(bf_str->as_byte_view());
		} else if (const auto nm = font_dict.get_name("Name")) {
			summary.name = std::string(*nm);
		} else if (!fallback_key.empty()) {
			summary.name = std::string(fallback_key);
		}

		// Extract `/Encoding`:
		if (const PdfValue* enc_val = font_dict.get("Encoding");
			enc_val != nullptr && !enc_val->is_null()) {
			auto enc_res = resolve_value(*enc_val);
			if (enc_res.has_value()) {
				if (const auto enc_name = enc_res->as_name()) {
					summary.encoding = std::string(*enc_name);
				} else if (const PdfDict* enc_dict = enc_res->as_dict()) {
					summary.has_differences = enc_dict->contains("Differences");
					if (const auto base_enc = enc_dict->get_name("BaseEncoding")) {
						summary.encoding = std::string(*base_enc);
					} else {
						summary.encoding = "Custom";
					}
				}
			}
		}

		if (const PdfValue* tu_val = font_dict.get("ToUnicode");
			tu_val != nullptr && !tu_val->is_null()) {
			summary.has_to_unicode = true;
		}

		summary.is_embedded = has_embedded_font_file(font_dict);
		if (summary.subtype == "Type3" && font_dict.contains("CharProcs")) {
			summary.is_embedded = true;
		}

		// Check `/DescendantFonts` for `/Subtype /Type0` composite fonts:
		if (const PdfValue* desc_val = font_dict.get("DescendantFonts");
			desc_val != nullptr && !desc_val->is_null()) {
			auto desc_res = resolve_value(*desc_val);
			if (desc_res.has_value()) {
				if (const PdfArray* desc_arr = desc_res->as_array();
					desc_arr != nullptr && !desc_arr->empty()) {
					auto cid_res = resolve_value((*desc_arr)[0]);
					if (cid_res.has_value()) {
						if (const PdfDict* cid_dict = cid_res->as_dict()) {
							if (has_embedded_font_file(*cid_dict)) {
								summary.is_embedded = true;
							}
							if (summary.name.empty()) {
								if (const auto cid_bf = cid_dict->get_name("BaseFont")) {
									summary.name = std::string(*cid_bf);
								}
							}
						}
					}
				}
			}
		}

		summary.base_font = summary.name;
		summary.is_subset = has_subset_font_prefix(summary.name);
		summary.clean_name = summary.is_subset ? summary.name.substr(7u) : summary.name;

		const auto dup_it = std::find_if(
			description_.fonts.begin(),
			description_.fonts.end(),
			[&summary](const FontSummary& existing) noexcept {
				return existing.name == summary.name &&
					existing.subtype == summary.subtype &&
					existing.encoding == summary.encoding &&
					existing.is_embedded == summary.is_embedded &&
					existing.is_subset == summary.is_subset;
			});
		if (dup_it == description_.fonts.end()) {
			description_.fonts.push_back(std::move(summary));
		} else if (summary.has_to_unicode) {
			dup_it->has_to_unicode = true;
		}
	};

	constexpr size_t kMaxResourceEntriesPerDict = 16'384u;
	auto scan_resources_for_fonts = [&](auto& self,
										const PdfDict& resources,
										size_t depth) -> PdfResult<void> {
		if (depth > kMaxFormResourceDepth) {
			return make_doc_error(
				ErrorCode::RecursionLimitExceeded,
				DiagnosticId::RecursionLimitExceeded,
				"Form XObject /Resources nesting exceeds maximum depth");
		}

		if (const PdfValue* font_map_val = resources.get("Font");
			font_map_val != nullptr && !font_map_val->is_null()) {
			auto font_map_res = resolve_value(*font_map_val);
			if (font_map_res.has_value()) {
				if (const PdfDict* font_map = font_map_res->as_dict()) {
					if (font_map->size() > kMaxResourceEntriesPerDict) {
						return make_doc_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedDictionary,
							"/Resources /Font dictionary exceeds maximum entry count");
					}
					for (const auto& [res_key, font_v] : *font_map) {
						PdfValue f_val;
						if (const auto f_ref = font_v.as_ref()) {
							if (!visited_font_objs.insert(f_ref->obj_num).second) {
								continue;
							}
							std::vector<IndirectRef> hop_refs;
							hop_refs.reserve(kMaxIndirectRefDepth);
							auto f_res = resolve_object_with_visited(*f_ref, hop_refs);
							if (!f_res.has_value()) {
								continue;
							}
							bool duplicate_alias = false;
							for (size_t h = 1u; h < hop_refs.size(); ++h) {
								if (!visited_font_objs.insert(hop_refs[h].obj_num).second) {
									duplicate_alias = true;
									break;
								}
							}
							if (duplicate_alias) {
								continue;
							}
							f_val = std::move(*f_res);
						} else {
							auto f_res = resolve_value(font_v);
							if (!f_res.has_value()) {
								continue;
							}
							f_val = std::move(*f_res);
						}
						if (const PdfDict* f_dict = f_val.as_dict()) {
							inspect_font_dict(*f_dict, res_key.view());
						}
					}
				}
			}
		}

		if (const PdfValue* xobj_map_val = resources.get("XObject");
			xobj_map_val != nullptr && !xobj_map_val->is_null()) {
			auto xobj_map_res = resolve_value(*xobj_map_val);
			if (xobj_map_res.has_value()) {
				if (const PdfDict* xobj_map = xobj_map_res->as_dict()) {
					if (xobj_map->size() > kMaxResourceEntriesPerDict) {
						return make_doc_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedDictionary,
							"/Resources /XObject dictionary exceeds maximum entry count");
					}
					for (const auto& [x_key, x_val] : *xobj_map) {
						PdfValue resolved_x;
						if (const auto x_ref = x_val.as_ref()) {
							if (!visited_xobj_objs.insert(x_ref->obj_num).second) {
								continue;
							}
							std::vector<IndirectRef> hop_refs;
							hop_refs.reserve(kMaxIndirectRefDepth);
							auto x_res = resolve_object_with_visited(*x_ref, hop_refs);
							if (!x_res.has_value()) {
								continue;
							}
							bool duplicate_alias = false;
							for (size_t h = 1u; h < hop_refs.size(); ++h) {
								if (!visited_xobj_objs.insert(hop_refs[h].obj_num).second) {
									duplicate_alias = true;
									break;
								}
							}
							if (duplicate_alias) {
								continue;
							}
							resolved_x = std::move(*x_res);
						} else {
							auto x_res = resolve_value(x_val);
							if (!x_res.has_value()) {
								continue;
							}
							resolved_x = std::move(*x_res);
						}
						if (const PdfStream* x_stm = resolved_x.as_stream()) {
							if (x_stm->dict.get_name("Subtype").value_or("") == "Form") {
								if (const PdfValue* form_res_v = x_stm->dict.get("Resources")) {
									auto form_res = resolve_value(*form_res_v);
									if (form_res.has_value()) {
										if (const PdfDict* form_res_dict = form_res->as_dict()) {
											RECTO_PDF_RETURN_IF_ERROR(
												self(self, *form_res_dict, depth + 1u));
										}
									}
								}
							}
						}
					}
				}
			}
		}
		return {};
	};

	std::unordered_set<const PdfDict*> scanned_resource_dicts;
	for (const ResolvedPageRecord& page_rec : page_records_) {
		if (page_rec.resources != nullptr &&
			scanned_resource_dicts.insert(page_rec.resources.get()).second) {
			RECTO_PDF_RETURN_IF_ERROR(
				scan_resources_for_fonts(scan_resources_for_fonts, *page_rec.resources, 0u));
		}
	}

	if (const PdfValue* acro_val = catalog_.get("AcroForm");
		acro_val != nullptr && !acro_val->is_null()) {
		auto acro_res = resolve_value(*acro_val);
		if (acro_res.has_value()) {
			if (const PdfDict* acro_dict = acro_res->as_dict()) {
				if (const PdfValue* dr_val = acro_dict->get("DR")) {
					auto dr_res = resolve_value(*dr_val);
					if (dr_res.has_value()) {
						if (const PdfDict* dr_dict = dr_res->as_dict()) {
							RECTO_PDF_RETURN_IF_ERROR(
								scan_resources_for_fonts(scan_resources_for_fonts, *dr_dict, 0u));
						}
					}
				}
			}
		}
	}

	return {};
}

PdfResult<PageDescription> Document::page_description(size_t page_index) const noexcept
{
	if (needs_password()) {
		return make_doc_error(
			ErrorCode::NeedsPassword,
			DiagnosticId::NeedsPassword,
			"Cannot query page description before document is authenticated");
	}
	if (page_index >= page_records_.size()) {
		return make_doc_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedDictionary,
			"Page index is out of range");
	}
	return page_records_[page_index].description;
}

PdfResult<std::vector<PageLink>> Document::page_links(size_t page_index) const noexcept
{
	try {
		if (needs_password()) {
			return make_doc_error(
				ErrorCode::NeedsPassword,
				DiagnosticId::NeedsPassword,
				"Cannot query page links before document is authenticated");
		}
		if (page_index >= page_records_.size()) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"Page index is out of range");
		}
		return page_records_[page_index].links;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while copying page links");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while querying page links");
	}
}

PdfResult<std::vector<uint8_t>> Document::decoded_page_contents(
	size_t page_index) const noexcept
{
	try {
		if (needs_password()) {
			return make_doc_error(
				ErrorCode::NeedsPassword,
				DiagnosticId::NeedsPassword,
				"Cannot decode page contents before document is authenticated");
		}
		if (page_index >= page_records_.size()) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"Page index is out of range");
		}
		const ResolvedPageRecord& rec = page_records_[page_index];
		if (rec.cached_contents.has_value()) {
			return *rec.cached_contents;
		}
		return decode_page_contents_from_dict(rec.page_dict);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while decoding page contents");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while decoding page contents");
	}
}

PdfResult<PdfDict> Document::page_resources(size_t page_index) const noexcept
{
	try {
		if (needs_password()) {
			return make_doc_error(
				ErrorCode::NeedsPassword,
				DiagnosticId::NeedsPassword,
				"Cannot query page resources before document is authenticated");
		}
		if (page_index >= page_records_.size()) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"Page index is out of range");
		}
		if (page_records_[page_index].resources == nullptr) {
			return PdfDict{};
		}
		return *page_records_[page_index].resources;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while cloning page resources");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while querying page resources");
	}
}

PdfResult<PdfDict> Document::page_dict(size_t page_index) const noexcept
{
	try {
		if (needs_password()) {
			return make_doc_error(
				ErrorCode::NeedsPassword,
				DiagnosticId::NeedsPassword,
				"Cannot query page dictionary before document is authenticated");
		}
		if (page_index >= page_records_.size()) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"Page index is out of range");
		}
		return page_records_[page_index].page_dict;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while cloning page dictionary");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while querying page dictionary");
	}
}

} // namespace recto::pdf
