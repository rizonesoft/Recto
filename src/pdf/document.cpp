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

[[nodiscard]] std::string decode_xml_entities(
	std::string_view text,
	size_t* inout_scan_steps = nullptr)
{
	std::string out;
	out.reserve(text.size());
	size_t local_steps = 0u;
	size_t i = 0;
	while (i < text.size()) {
		if (text[i] == '&') {
			const size_t search_limit = std::min<size_t>(text.size(), i + 11u);
			size_t semi = std::string_view::npos;
			for (size_t j = i + 1u; j < search_limit; ++j) {
				++local_steps;
				if (text[j] == ';') {
					semi = j;
					break;
				}
			}
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
	if (inout_scan_steps != nullptr) {
		*inout_scan_steps += local_steps;
	}
	return out;
}

constexpr std::string_view kXmpNsDc = "http://purl.org/dc/elements/1.1/";
constexpr std::string_view kXmpNsPdf = "http://ns.adobe.com/pdf/1.3/";
constexpr std::string_view kXmpNsXmp = "http://ns.adobe.com/xap/1.0/";
constexpr std::string_view kXmpNsRdf = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
constexpr std::string_view kXmpNsXml = "http://www.w3.org/XML/1998/namespace";

enum class XmpNsKind : uint8_t {
	Other = 0,
	Dc,
	Pdf,
	Xmp,
	Rdf,
	Xml,
};

[[nodiscard]] constexpr XmpNsKind classify_xmp_ns_uri(std::string_view uri) noexcept
{
	if (uri == kXmpNsDc) {
		return XmpNsKind::Dc;
	}
	if (uri == kXmpNsPdf) {
		return XmpNsKind::Pdf;
	}
	if (uri == kXmpNsXmp) {
		return XmpNsKind::Xmp;
	}
	if (uri == kXmpNsRdf) {
		return XmpNsKind::Rdf;
	}
	if (uri == kXmpNsXml) {
		return XmpNsKind::Xml;
	}
	return XmpNsKind::Other;
}

struct ExtractedXmpPacket {
	std::string title{};
	std::string author{};
	std::string subject{};
	std::string pdf_keywords{};
	std::string dc_subject{};
	std::string creator_tool{};
	std::string producer{};
	std::string create_date{};
	std::string modify_date{};
};

// Single-pass XMP metadata extractor using O(1) scoped namespace bindings
// (`std::unordered_map<std::string_view, XmpNsKind>` with an undo trail per
// scope). Resolves `xmlns` default namespace and `xmlns:<prefix>` declarations
// across nested element scopes, enforces W3C reserved `xml:lang="x-default"`
// semantics on `<rdf:Alt>` items, and supports shorthand attributes.
[[nodiscard]] ExtractedXmpPacket extract_xmp_metadata_packet(
	std::string_view xml,
	size_t* inout_entity_scan_steps = nullptr)
{
	struct XmlAttr {
		std::string_view name{};
		std::string value{};
	};

	struct PrefixUndo {
		std::string_view prefix{};
		std::optional<XmpNsKind> prev_kind{};
	};

	struct XmlScope {
		std::string_view qname{};
		XmpNsKind default_ns{XmpNsKind::Other};
		size_t undo_start{0u};
	};

	constexpr size_t kMaxXmlDepth = 64u;
	constexpr size_t kMaxActivePrefixBindings = 4096u;

	std::unordered_map<std::string_view, XmpNsKind> active_prefixes;
	active_prefixes.reserve(16u);
	active_prefixes.emplace("dc", XmpNsKind::Dc);
	active_prefixes.emplace("pdf", XmpNsKind::Pdf);
	active_prefixes.emplace("xmp", XmpNsKind::Xmp);
	active_prefixes.emplace("xap", XmpNsKind::Xmp);
	active_prefixes.emplace("rdf", XmpNsKind::Rdf);

	std::vector<PrefixUndo> undo_stack;
	std::vector<XmlScope> scopes;
	scopes.reserve(kMaxXmlDepth + 1u);
	scopes.push_back(XmlScope{{}, XmpNsKind::Other, 0u});

	auto bind_prefix = [&](std::string_view prefix, XmpNsKind kind) {
		// Per W3C XML Namespaces section 3 ("Declaring Namespaces"), `xml` is
		// permanently bound to `http://www.w3.org/XML/1998/namespace` and no
		// other prefix may bind to that namespace URI.
		if (prefix.empty() || prefix == "xml" || prefix == "xmlns") {
			return;
		}
		if (kind == XmpNsKind::Xml) {
			kind = XmpNsKind::Other;
		}
		const auto it = active_prefixes.find(prefix);
		if (it == active_prefixes.end()) {
			// Unrelated namespace bindings that do not shadow an active XMP/RDF
			// prefix need not be stored at all because missing keys already map
			// to `XmpNsKind::Other`.
			if (kind == XmpNsKind::Other || active_prefixes.size() >= kMaxActivePrefixBindings) {
				return;
			}
			undo_stack.push_back(PrefixUndo{prefix, std::nullopt});
			active_prefixes.emplace(prefix, kind);
		} else if (it->second != kind) {
			undo_stack.push_back(PrefixUndo{prefix, it->second});
			it->second = kind;
		}
	};

	auto lookup_prefix = [&](std::string_view prefix) noexcept -> XmpNsKind {
		if (prefix == "xml") {
			return XmpNsKind::Xml;
		}
		const auto it = active_prefixes.find(prefix);
		return (it != active_prefixes.end()) ? it->second : XmpNsKind::Other;
	};

	auto pop_scope = [&]() {
		const size_t target_undo = scopes.back().undo_start;
		while (undo_stack.size() > target_undo) {
			const PrefixUndo u = undo_stack.back();
			undo_stack.pop_back();
			if (u.prev_kind.has_value()) {
				active_prefixes[u.prefix] = *u.prev_kind;
			} else {
				active_prefixes.erase(u.prefix);
			}
		}
		scopes.pop_back();
	};

	auto is_xml_space = [](char ch) noexcept -> bool {
		return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
	};

	auto is_xml_name_char = [&](char ch) noexcept -> bool {
		return !is_xml_space(ch) && ch != '>' && ch != '/' && ch != '=' && ch != '<' &&
			ch != '"' && ch != '\'';
	};

	auto split_qname = [](std::string_view qn) noexcept -> std::pair<std::string_view, std::string_view> {
		const size_t colon = qn.find(':');
		if (colon == std::string_view::npos) {
			return {{}, qn};
		}
		return {qn.substr(0u, colon), qn.substr(colon + 1u)};
	};

	ExtractedXmpPacket elem_out{};
	ExtractedXmpPacket attr_out{};

	auto match_slot = [](
						  ExtractedXmpPacket& pkt,
						  XmpNsKind ns,
						  std::string_view local) noexcept -> std::string* {
		if (ns == XmpNsKind::Dc) {
			if (local == "title") {
				return &pkt.title;
			}
			if (local == "creator") {
				return &pkt.author;
			}
			if (local == "description") {
				return &pkt.subject;
			}
			if (local == "subject") {
				return &pkt.dc_subject;
			}
		} else if (ns == XmpNsKind::Pdf) {
			if (local == "Keywords") {
				return &pkt.pdf_keywords;
			}
			if (local == "Producer") {
				return &pkt.producer;
			}
		} else if (ns == XmpNsKind::Xmp) {
			if (local == "CreatorTool") {
				return &pkt.creator_tool;
			}
			if (local == "CreateDate") {
				return &pkt.create_date;
			}
			if (local == "ModifyDate") {
				return &pkt.modify_date;
			}
		}
		return nullptr;
	};

	struct LiItem {
		std::string text{};
		bool is_x_default{false};
	};

	std::optional<size_t> target_depth;
	std::string* active_target_slot = nullptr;
	std::optional<size_t> active_li_depth;
	bool active_li_x_default = false;
	bool in_rdf_alt = false;
	std::string current_li_text;
	std::string direct_target_text;
	std::vector<LiItem> li_items;

	size_t pos = 0u;
	const size_t n = xml.size();
	while (pos < n) {
		const size_t lt = xml.find('<', pos);
		const size_t text_end = (lt == std::string_view::npos) ? n : lt;
		if (text_end > pos && target_depth.has_value()) {
			const std::string decoded_chunk = decode_xml_entities(
				xml.substr(pos, text_end - pos),
				inout_entity_scan_steps);
			if (active_li_depth.has_value()) {
				current_li_text += decoded_chunk;
			} else {
				direct_target_text += decoded_chunk;
			}
		}
		if (lt == std::string_view::npos) {
			break;
		}
		pos = lt;

		const std::string_view rem = xml.substr(pos);
		if (rem.starts_with("<!--")) {
			const size_t end_comment = xml.find("-->", pos + 4u);
			if (end_comment == std::string_view::npos) {
				break;
			}
			pos = end_comment + 3u;
			continue;
		}
		if (rem.starts_with("<![CDATA[")) {
			const size_t end_cdata = xml.find("]]>", pos + 9u);
			if (end_cdata == std::string_view::npos) {
				break;
			}
			if (target_depth.has_value()) {
				const std::string_view cdata_chunk =
					xml.substr(pos + 9u, end_cdata - (pos + 9u));
				if (active_li_depth.has_value()) {
					current_li_text.append(cdata_chunk);
				} else {
					direct_target_text.append(cdata_chunk);
				}
			}
			pos = end_cdata + 3u;
			continue;
		}
		if (rem.starts_with("<?")) {
			const size_t end_pi = xml.find("?>", pos + 2u);
			if (end_pi == std::string_view::npos) {
				break;
			}
			pos = end_pi + 2u;
			continue;
		}
		if (rem.starts_with("<!")) {
			const size_t end_decl = xml.find('>', pos + 2u);
			if (end_decl == std::string_view::npos) {
				break;
			}
			pos = end_decl + 1u;
			continue;
		}

		// Closing tag `</qname>`:
		if (rem.starts_with("</")) {
			const size_t gt = xml.find('>', pos + 2u);
			if (gt == std::string_view::npos) {
				break;
			}
			const std::string_view close_qname =
				trim_ascii_whitespace(xml.substr(pos + 2u, gt - (pos + 2u)));
			pos = gt + 1u;

			size_t match_idx = scopes.size();
			bool found_match = false;
			while (match_idx > 1u) {
				--match_idx;
				if (scopes[match_idx].qname == close_qname) {
					found_match = true;
					break;
				}
			}
			if (found_match) {
				while (scopes.size() > match_idx) {
					const size_t closing_depth = scopes.size() - 1u;
					if (active_li_depth.has_value() && closing_depth == *active_li_depth) {
						std::string trimmed_li(trim_ascii_whitespace(current_li_text));
						if (!trimmed_li.empty()) {
							li_items.push_back(LiItem{std::move(trimmed_li), active_li_x_default});
						}
						current_li_text.clear();
						active_li_depth.reset();
						active_li_x_default = false;
					}
					if (target_depth.has_value() && closing_depth == *target_depth) {
						std::string resolved_val;
						if (!li_items.empty()) {
							if (in_rdf_alt) {
								const std::string* chosen = &li_items.front().text;
								for (const LiItem& item : li_items) {
									if (item.is_x_default) {
										chosen = &item.text;
										break;
									}
								}
								resolved_val = *chosen;
							} else {
								resolved_val = li_items.front().text;
								for (size_t i = 1u; i < li_items.size(); ++i) {
									resolved_val += ", ";
									resolved_val += li_items[i].text;
								}
							}
						} else {
							resolved_val = std::string(trim_ascii_whitespace(direct_target_text));
						}
						if (!resolved_val.empty() && active_target_slot != nullptr &&
							active_target_slot->empty()) {
							*active_target_slot = std::move(resolved_val);
						}
						target_depth.reset();
						active_target_slot = nullptr;
						active_li_depth.reset();
						in_rdf_alt = false;
						direct_target_text.clear();
						li_items.clear();
					}
					pop_scope();
				}
			}
			continue;
		}

		// Opening tag `<qname ...>` or `<qname .../>`:
		size_t cur = pos + 1u;
		while (cur < n && is_xml_space(xml[cur])) {
			++cur;
		}
		const size_t name_start = cur;
		while (cur < n && is_xml_name_char(xml[cur])) {
			++cur;
		}
		const std::string_view tag_qname = xml.substr(name_start, cur - name_start);
		if (tag_qname.empty()) {
			++pos;
			continue;
		}

		XmlScope child_scope{};
		child_scope.qname = tag_qname;
		child_scope.default_ns = scopes.back().default_ns;
		child_scope.undo_start = undo_stack.size();

		std::vector<XmlAttr> non_ns_attrs;
		bool self_closing = false;
		bool malformed_tag = false;
		while (true) {
			while (cur < n && is_xml_space(xml[cur])) {
				++cur;
			}
			if (cur >= n) {
				malformed_tag = true;
				break;
			}
			if (xml[cur] == '>') {
				++cur;
				break;
			}
			if (xml[cur] == '/' && cur + 1u < n && xml[cur + 1u] == '>') {
				self_closing = true;
				cur += 2u;
				break;
			}
			const size_t attr_start = cur;
			while (cur < n && is_xml_name_char(xml[cur])) {
				++cur;
			}
			const std::string_view attr_name = xml.substr(attr_start, cur - attr_start);
			if (attr_name.empty()) {
				++cur;
				continue;
			}
			while (cur < n && is_xml_space(xml[cur])) {
				++cur;
			}
			std::string attr_val;
			if (cur < n && xml[cur] == '=') {
				++cur;
				while (cur < n && is_xml_space(xml[cur])) {
					++cur;
				}
				if (cur < n && (xml[cur] == '"' || xml[cur] == '\'')) {
					const char quote = xml[cur++];
					const size_t val_start = cur;
					const size_t val_end = xml.find(quote, val_start);
					if (val_end == std::string_view::npos) {
						malformed_tag = true;
						break;
					}
					attr_val = decode_xml_entities(
						trim_ascii_whitespace(xml.substr(val_start, val_end - val_start)),
						inout_entity_scan_steps);
					cur = val_end + 1u;
				} else {
					const size_t val_start = cur;
					while (cur < n && !is_xml_space(xml[cur]) && xml[cur] != '>' && xml[cur] != '/') {
						++cur;
					}
					attr_val = decode_xml_entities(
						trim_ascii_whitespace(xml.substr(val_start, cur - val_start)),
						inout_entity_scan_steps);
				}
			}
			if (attr_name == "xmlns") {
				child_scope.default_ns = classify_xmp_ns_uri(attr_val);
			} else if (attr_name.starts_with("xmlns:")) {
				bind_prefix(attr_name.substr(6u), classify_xmp_ns_uri(attr_val));
			} else {
				non_ns_attrs.push_back(XmlAttr{attr_name, std::move(attr_val)});
			}
		}
		if (malformed_tag) {
			scopes.push_back(child_scope);
			pop_scope();
			break;
		}
		pos = cur;

		if (scopes.size() >= kMaxXmlDepth) {
			scopes.push_back(child_scope);
			pop_scope();
			continue;
		}

		scopes.push_back(child_scope);
		const size_t new_depth = scopes.size() - 1u;

		const auto [elem_prefix, elem_local] = split_qname(tag_qname);
		const XmpNsKind elem_ns = elem_prefix.empty()
			? child_scope.default_ns
			: lookup_prefix(elem_prefix);

		// Check shorthand attributes on any element (e.g. `rdf:Description dc:title="..."`):
		for (const XmlAttr& a : non_ns_attrs) {
			const auto [attr_prefix, attr_local] = split_qname(a.name);
			if (attr_prefix.empty() || a.value.empty()) {
				continue;
			}
			const XmpNsKind attr_ns = lookup_prefix(attr_prefix);
			if (std::string* slot = match_slot(attr_out, attr_ns, attr_local);
				slot != nullptr && slot->empty()) {
				*slot = a.value;
			}
		}

		if (!target_depth.has_value()) {
			if (!self_closing) {
				if (std::string* slot = match_slot(elem_out, elem_ns, elem_local);
					slot != nullptr && slot->empty()) {
					target_depth = new_depth;
					active_target_slot = slot;
					active_li_depth.reset();
					in_rdf_alt = false;
					current_li_text.clear();
					direct_target_text.clear();
					li_items.clear();
				}
			}
		} else {
			if (elem_ns == XmpNsKind::Rdf && elem_local == "Alt") {
				in_rdf_alt = true;
			} else if (elem_ns == XmpNsKind::Rdf && elem_local == "li" && !self_closing &&
					   !active_li_depth.has_value()) {
				active_li_depth = new_depth;
				current_li_text.clear();
				active_li_x_default = false;
				for (const XmlAttr& a : non_ns_attrs) {
					const auto [ap, al] = split_qname(a.name);
					if (al == "lang" && lookup_prefix(ap) == XmpNsKind::Xml &&
						a.value == "x-default") {
						active_li_x_default = true;
						break;
					}
				}
			}
		}

		if (self_closing) {
			pop_scope();
		}
	}

	auto merge_fallback = [](std::string& primary, std::string& fallback) {
		if (primary.empty() && !fallback.empty()) {
			primary = std::move(fallback);
		}
	};
	merge_fallback(elem_out.title, attr_out.title);
	merge_fallback(elem_out.author, attr_out.author);
	merge_fallback(elem_out.subject, attr_out.subject);
	merge_fallback(elem_out.pdf_keywords, attr_out.pdf_keywords);
	merge_fallback(elem_out.dc_subject, attr_out.dc_subject);
	merge_fallback(elem_out.creator_tool, attr_out.creator_tool);
	merge_fallback(elem_out.producer, attr_out.producer);
	merge_fallback(elem_out.create_date, attr_out.create_date);
	merge_fallback(elem_out.modify_date, attr_out.modify_date);
	return elem_out;
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
	RECTO_PDF_TRY_REF(
		parsed_xref,
		XrefTable::parse(file_bytes_, kMaxXrefTotalEntries, /*allow_recovery=*/true));
	xref_ = std::move(parsed_xref);
	description_.pdf_version = std::string(xref_.pdf_version());
	description_.is_recovered = xref_.recovered();

	RECTO_PDF_TRY_REF(
		sec,
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
			if (!structure_loaded_) {
				RECTO_PDF_RETURN_IF_ERROR(load_document_structure());
			}
			return {};
		}
		RECTO_PDF_RETURN_IF_ERROR(security_handler_->authenticate(password));
		description_.is_authenticated = security_handler_->is_authenticated();
		description_.unlocked_as_owner = security_handler_->unlocked_as_owner();
		description_.permissions = security_handler_->permissions();

		if (!structure_loaded_ && description_.is_authenticated) {
			RECTO_PDF_RETURN_IF_ERROR(load_document_structure());
		}
		return {};
	} catch (const std::bad_alloc&) {
		if (!structure_loaded_) {
			description_.is_authenticated = false;
			description_.unlocked_as_owner = false;
			description_.permissions = PdfPermissions{};
		}
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while authenticating PDF document");
	} catch (...) {
		if (!structure_loaded_) {
			description_.is_authenticated = false;
			description_.unlocked_as_owner = false;
			description_.permissions = PdfPermissions{};
		}
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while authenticating PDF document");
	}
}

namespace {

[[nodiscard]] constexpr uint64_t make_ref_cache_key(IndirectRef ref) noexcept
{
	return (static_cast<uint64_t>(ref.obj_num) << 16u) |
		static_cast<uint64_t>(ref.gen_num);
}

// Fast-path scanner for uncompressed indirect alias objects (`N G obj M K R endobj`),
// including optional PDF `%` comments between tokens. Avoids allocating a full
// `Parser` and `Lexer` lookahead vector when stepping through uncompressed
// indirect reference alias chains.
[[nodiscard]] std::optional<IndirectRef> try_read_uncompressed_ref_alias(
	std::span<const uint8_t> file_bytes,
	uint64_t byte_offset,
	IndirectRef expected_header) noexcept
{
	if (byte_offset >= file_bytes.size()) {
		return std::nullopt;
	}
	size_t p = static_cast<size_t>(byte_offset);
	const size_t n = file_bytes.size();

	auto skip_ws_and_comments = [&]() noexcept {
		while (p < n) {
			if (Lexer::is_pdf_whitespace(file_bytes[p])) {
				++p;
			} else if (file_bytes[p] == '%') {
				++p;
				while (p < n && file_bytes[p] != '\r' && file_bytes[p] != '\n') {
					++p;
				}
			} else {
				break;
			}
		}
	};

	auto read_u64 = [&](uint64_t& out) noexcept -> bool {
		skip_ws_and_comments();
		if (p >= n || file_bytes[p] < '0' || file_bytes[p] > '9') {
			return false;
		}
		uint64_t v = 0;
		size_t digits = 0;
		while (p < n && file_bytes[p] >= '0' && file_bytes[p] <= '9') {
			v = v * 10u + static_cast<uint64_t>(file_bytes[p] - '0');
			++p;
			if (++digits > 12u) {
				return false;
			}
		}
		if (p < n && Lexer::is_pdf_regular(file_bytes[p])) {
			return false;
		}
		out = v;
		return true;
	};

	auto match_kw = [&](std::string_view kw) noexcept -> bool {
		skip_ws_and_comments();
		if (p + kw.size() > n) {
			return false;
		}
		if (std::memcmp(file_bytes.data() + p, kw.data(), kw.size()) != 0) {
			return false;
		}
		if (p + kw.size() < n && Lexer::is_pdf_regular(file_bytes[p + kw.size()])) {
			return false;
		}
		p += kw.size();
		return true;
	};

	uint64_t hdr_obj = 0;
	uint64_t hdr_gen = 0;
	if (!read_u64(hdr_obj) || !read_u64(hdr_gen) || !match_kw("obj")) {
		return std::nullopt;
	}
	if (hdr_obj != expected_header.obj_num || hdr_gen != expected_header.gen_num) {
		return std::nullopt;
	}

	uint64_t target_obj = 0;
	uint64_t target_gen = 0;
	if (!read_u64(target_obj) || !read_u64(target_gen) || !match_kw("R") || !match_kw("endobj")) {
		return std::nullopt;
	}
	if (target_obj == 0u ||
		target_obj > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) ||
		target_gen > static_cast<uint64_t>(std::numeric_limits<uint16_t>::max())) {
		return std::nullopt;
	}
	return IndirectRef{
		static_cast<uint32_t>(target_obj),
		static_cast<uint16_t>(target_gen)};
}

} // namespace

PdfResult<Document::SingleHopResult> Document::resolve_single_indirect_hop(
	IndirectRef current,
	const std::vector<IndirectRef>& visited_refs) const
{
	const XrefEntry* entry = xref_.find(current.obj_num);
	if (entry == nullptr || entry->is_free()) {
		return SingleHopResult{std::nullopt, PdfValue(PdfNull{})};
	}
	if (entry->is_uncompressed()) {
		if (entry->generation != current.gen_num) {
			return SingleHopResult{std::nullopt, PdfValue(PdfNull{})};
		}
		if (const auto fast_alias = try_read_uncompressed_ref_alias(
				file_bytes_,
				entry->byte_offset,
				current)) {
			return SingleHopResult{*fast_alias, PdfValue(PdfNull{})};
		}
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
				return SingleHopResult{std::nullopt, PdfValue(PdfNull{})};
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
			// Sibling objects share one decrypted, decoded container per document (decrypted_obj_stm_cache_); it is
			// read through the same routines as XrefTable's and SecurityHandler's (parse_object_stream,
			// extract_object_stream_member).
			const CachedObjStream* cached_stm = decrypted_obj_stm_cache_.find(stm_obj_num);
			CachedObjStream uncached; // holds a container too large for the cache, for this one read
			if (cached_stm == nullptr) {
				RECTO_PDF_TRY(
					IndirectObject raw_stm_obj,
					xref_.read_indirect_object(file_bytes_, stm_ref));
				// Indirect /Filter, /DecodeParms, /N and /First resolve with this container already visited (a cycle
				// back into it fails cleanly).
				const IndirectObjectResolver stm_resolver = [&](IndirectRef r) -> PdfResult<PdfValue> {
					std::vector<IndirectRef> hop_visited = visited_refs;
					hop_visited.push_back(stm_ref);
					return resolve_object_with_visited(r, hop_visited);
				};
				RECTO_PDF_RETURN_IF_ERROR(
					security_handler_->decrypt_object_values(
						raw_stm_obj.value,
						stm_ref,
						/*is_inside_obj_stm=*/false,
						&stm_resolver));
				const PdfStream* obj_stm = raw_stm_obj.value.as_stream();
				bool is_objstm = false;
				if (obj_stm != nullptr) {
					RECTO_PDF_TRY(const bool type_ok, is_object_stream(obj_stm->dict, &stm_resolver));
					is_objstm = type_ok;
				}
				if (!is_objstm) {
					return make_doc_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedObject,
						"Compressed object target is not a /Type /ObjStm stream",
						std::nullopt,
						stm_obj_num,
						0u);
				}
				RECTO_PDF_TRY(
					std::vector<uint8_t> decoded_stm,
					recto::pdf::decode_stream(*obj_stm, kMaxDecodedStreamBytes, &stm_resolver));
				RECTO_PDF_TRY_REF(
					cached,
					parse_object_stream(obj_stm->dict, std::move(decoded_stm), stm_obj_num, &stm_resolver));
				cached_stm = &decrypted_obj_stm_cache_.insert(stm_obj_num, std::move(cached), uncached);
			}
			RECTO_PDF_TRY_REF(
				inner_val,
				extract_object_stream_member(*cached_stm, stm_index, current));
			return SingleHopResult{std::nullopt, std::move(inner_val)};
		}

		RECTO_PDF_TRY(
			IndirectObject raw_obj,
			xref_.read_indirect_object(file_bytes_, current));
		if (const auto next_ref = raw_obj.value.as_ref()) {
			return SingleHopResult{*next_ref, PdfValue(PdfNull{})};
		}
		const IndirectObjectResolver filter_resolver = [&](IndirectRef r) -> PdfResult<PdfValue> {
			std::vector<IndirectRef> hop_visited = visited_refs;
			hop_visited.push_back(current);
			return resolve_object_with_visited(r, hop_visited);
		};
		RECTO_PDF_RETURN_IF_ERROR(
			security_handler_->decrypt_object_values(
				raw_obj.value,
				current,
				/*is_inside_obj_stm=*/false,
				&filter_resolver));
		return SingleHopResult{std::nullopt, std::move(raw_obj.value)};
	}

	// Unencrypted document:
	RECTO_PDF_TRY(
		IndirectObject raw_obj,
		xref_.read_indirect_object(file_bytes_, current));
	if (const auto next_ref = raw_obj.value.as_ref()) {
		return SingleHopResult{*next_ref, PdfValue(PdfNull{})};
	}
	return SingleHopResult{std::nullopt, std::move(raw_obj.value)};
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
		RECTO_PDF_TRY(
			SingleHopResult step,
			resolve_single_indirect_hop(current, visited_refs));
		if (step.next_ref.has_value()) {
			current = *step.next_ref;
			continue;
		}
		return std::move(step.terminal_value);
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
	structure_loaded_ = false;
	catalog_ = PdfDict{};
	page_records_.clear();
	page_obj_to_index_.clear();
	named_dests_.clear();
	xmp_entity_scan_steps_ = 0u;
	description_.pages.clear();
	description_.outline.clear();
	description_.fonts.clear();
	description_.metadata = DocumentMetadata{};
	description_.pdf_version = std::string(xref_.pdf_version());

	bool structure_committed = false;
	struct StructureRollbackGuard {
		Document& doc;
		bool& committed;
		~StructureRollbackGuard() noexcept
		{
			if (!committed) {
				doc.structure_loaded_ = false;
				doc.catalog_ = PdfDict{};
				doc.page_records_.clear();
				doc.page_obj_to_index_.clear();
				doc.named_dests_.clear();
				doc.xmp_entity_scan_steps_ = 0u;
				doc.description_.pages.clear();
				doc.description_.outline.clear();
				doc.description_.fonts.clear();
				doc.description_.metadata = DocumentMetadata{};
				doc.description_.page_count = 0u;
				doc.description_.pdf_version = std::string(doc.xref_.pdf_version());
				if (doc.security_handler_.has_value()) {
					doc.description_.is_authenticated = false;
					doc.description_.unlocked_as_owner = false;
					doc.description_.permissions = PdfPermissions{};
				}
			}
		}
	} rollback_guard{*this, structure_committed};

	struct CachedChainResult {
		bool hit_cache{false};
		size_t hit_remaining_hops{0};
		PdfValue terminal_value{PdfNull{}};
	};

	auto resolve_indirect_chain_with_cache =
		[this](
			IndirectRef start_ref,
			const auto& cache_map,
			std::vector<IndirectRef>& out_hop_refs,
			auto& out_cached_val) -> PdfResult<CachedChainResult> {
		IndirectRef current = start_ref;
		while (true) {
			if (const auto hit_it = cache_map.find(make_ref_cache_key(current));
				hit_it != cache_map.end()) {
				const size_t total_hops = out_hop_refs.size() + hit_it->second.remaining_hops;
				if (total_hops > kMaxIndirectRefDepth) {
					return make_doc_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedObject,
						"Indirect object reference chain exceeds maximum depth of 32",
						std::nullopt,
						current.obj_num,
						current.gen_num);
				}
				out_cached_val = hit_it->second.value;
				return CachedChainResult{true, hit_it->second.remaining_hops, PdfValue(PdfNull{})};
			}
			if (std::find(out_hop_refs.begin(), out_hop_refs.end(), current) != out_hop_refs.end()) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedObject,
					"Cyclic indirect object reference chain detected",
					std::nullopt,
					current.obj_num,
					current.gen_num);
			}
			if (out_hop_refs.size() >= kMaxIndirectRefDepth) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedObject,
					"Indirect object reference chain exceeds maximum depth of 32",
					std::nullopt,
					current.obj_num,
					current.gen_num);
			}
			out_hop_refs.push_back(current);
			RECTO_PDF_TRY(
				SingleHopResult step,
				resolve_single_indirect_hop(current, out_hop_refs));
			if (step.next_ref.has_value()) {
				current = *step.next_ref;
				continue;
			}
			return CachedChainResult{false, 0u, std::move(step.terminal_value)};
		}
	};

	auto insert_cached_chain_entries =
		[](
			auto& cache_map,
			const std::vector<IndirectRef>& hop_refs,
			size_t hit_remaining_hops,
			const auto& val,
			size_t& inout_budget_bytes,
			size_t max_budget_bytes,
			size_t& inout_cached_entries,
			size_t max_entries,
			std::string_view err_msg) -> PdfResult<void> {
		using EntryType = typename std::decay_t<decltype(cache_map)>::mapped_type;
		constexpr size_t kEntryBytes = sizeof(uint64_t) + sizeof(EntryType);
		const size_t n = hop_refs.size();
		for (size_t idx = 0u; idx < n; ++idx) {
			const uint64_t hkey = make_ref_cache_key(hop_refs[idx]);
			const size_t rem_hops = (n - idx) + hit_remaining_hops;
			auto it = cache_map.find(hkey);
			if (it == cache_map.end()) {
				if (inout_cached_entries >= max_entries ||
					inout_budget_bytes + kEntryBytes > max_budget_bytes) {
					return make_doc_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedDictionary,
						err_msg);
				}
				++inout_cached_entries;
				inout_budget_bytes += kEntryBytes;
				cache_map.emplace(hkey, EntryType{val, rem_hops});
			} else if (rem_hops > it->second.remaining_hops) {
				it->second.remaining_hops = rem_hops;
			}
		}
		return {};
	};

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

	// Check `/Catalog /Version` override (ISO 32000-1 section 7.5.2 / 7.7.2:
	// overrides the file header version only if strictly later):
	if (const PdfValue* ver_val = catalog_.get("Version");
		ver_val != nullptr && !ver_val->is_null()) {
		auto ver_res = resolve_value(*ver_val);
		if (ver_res.has_value()) {
			std::string_view cat_ver_sv;
			if (const auto cat_ver = ver_res->as_name()) {
				cat_ver_sv = *cat_ver;
			} else if (const PdfString* cat_ver_str = ver_res->as_string()) {
				cat_ver_sv = cat_ver_str->as_byte_view();
			}
			if (is_valid_pdf_version_string(cat_ver_sv) &&
				cat_ver_sv > description_.pdf_version) {
				description_.pdf_version = std::string(cat_ver_sv);
			}
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

	constexpr size_t kMaxResourceDictBytes = 1u * 1024u * 1024u;
	constexpr size_t kMaxTotalResourcesBytes = 16u * 1024u * 1024u;
	constexpr size_t kMaxCachedResourceObjs = 65'536u;
	size_t total_retained_resources_bytes = 0u;
	size_t total_cached_resource_objs = 0u;
	size_t total_cached_resource_cat_objs = 0u;

	auto estimate_pdf_value_bytes = [](auto& self, const PdfValue& val, size_t depth) noexcept -> size_t {
		if (depth > kMaxPagesTreeDepth) {
			return sizeof(PdfValue);
		}
		size_t bytes = sizeof(PdfValue);
		if (const PdfString* s = val.as_string()) {
			bytes += s->span().size();
		} else if (const auto nm = val.as_name()) {
			bytes += nm->size();
		} else if (const PdfArray* arr = val.as_array()) {
			for (const PdfValue& elem : *arr) {
				bytes += self(self, elem, depth + 1u);
			}
		} else if (const PdfDict* dict = val.as_dict()) {
			for (const auto& [k, v] : *dict) {
				bytes += sizeof(PdfName) + k.view().size() + self(self, v, depth + 1u);
			}
		} else if (const PdfStream* stm = val.as_stream()) {
			bytes += stm->raw_bytes.size();
			for (const auto& [k, v] : stm->dict) {
				bytes += sizeof(PdfName) + k.view().size() + self(self, v, depth + 1u);
			}
		}
		return bytes;
	};

	struct CachedResDictEntry {
		std::shared_ptr<const PdfDict> value{};
		size_t remaining_hops{1u};
	};
	struct CachedResCatEntry {
		std::shared_ptr<const PdfValue> value{};
		size_t remaining_hops{1u};
	};
	std::unordered_map<uint64_t, CachedResDictEntry> parsed_resources_obj_cache;
	std::unordered_map<uint64_t, CachedResCatEntry> parsed_resource_cat_obj_cache;

	auto build_resolved_resources_dict = [&](const PdfDict& res_dict) -> PdfResult<std::shared_ptr<const PdfDict>> {
		// Per ISO 32000-1 section 7.7.3.4 ("Inheritance of Page Attributes"),
		// `/Resources` is inherited as a whole dictionary only when omitted on a
		// descendant node; an explicit `/Resources` dictionary on a child node
		// replaces the ancestor's `/Resources` dictionary rather than merging
		// individual categories. Resolve any indirect category sub-dictionaries
		// (`/Font`, `/XObject`, `/ExtGState`, etc.) so `page_resources` holds
		// direct category dictionaries while keeping individual resource entries
		// (`/F1 -> 5 0 R`) intact.
		constexpr size_t kMaxResourceCategories = 64u;
		if (res_dict.size() > kMaxResourceCategories) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"/Resources dictionary exceeds maximum category count");
		}
		PdfDict replaced_resources{};
		size_t dict_bytes = sizeof(PdfDict);
		for (const auto& [cat_key, cat_val] : res_dict) {
			PdfValue resolved_cat;
			if (const auto cat_ref = cat_val.as_ref()) {
				std::vector<IndirectRef> hop_refs;
				hop_refs.reserve(kMaxIndirectRefDepth);
				std::shared_ptr<const PdfValue> cached_cat;
				RECTO_PDF_TRY(
					CachedChainResult cat_chain_res,
					resolve_indirect_chain_with_cache(
						*cat_ref,
						parsed_resource_cat_obj_cache,
						hop_refs,
						cached_cat));
				if (!cat_chain_res.hit_cache) {
					const size_t cat_est =
						estimate_pdf_value_bytes(estimate_pdf_value_bytes, cat_chain_res.terminal_value, 0u);
					if (cat_est > kMaxResourceDictBytes ||
						total_retained_resources_bytes + cat_est > kMaxTotalResourcesBytes) {
						return make_doc_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedDictionary,
							"/Resources category dictionary exceeds maximum byte budget");
					}
					total_retained_resources_bytes += cat_est;
					cached_cat = std::make_shared<const PdfValue>(std::move(cat_chain_res.terminal_value));
				}
				RECTO_PDF_RETURN_IF_ERROR(insert_cached_chain_entries(
					parsed_resource_cat_obj_cache,
					hop_refs,
					cat_chain_res.hit_remaining_hops,
					cached_cat,
					total_retained_resources_bytes,
					kMaxTotalResourcesBytes,
					total_cached_resource_cat_objs,
					kMaxCachedResourceObjs,
					"/Resources category alias cache exceeds maximum byte budget"));
				RECTO_PDF_TRY(PdfValue cloned_cat, cached_cat->deep_clone());
				resolved_cat = std::move(cloned_cat);
			} else {
				RECTO_PDF_TRY(PdfValue r_cat, resolve_value(cat_val));
				resolved_cat = std::move(r_cat);
			}
			const size_t cat_bytes =
				sizeof(PdfName) + cat_key.view().size() +
				estimate_pdf_value_bytes(estimate_pdf_value_bytes, resolved_cat, 0u);
			if (dict_bytes + cat_bytes > kMaxResourceDictBytes ||
				total_retained_resources_bytes + dict_bytes + cat_bytes > kMaxTotalResourcesBytes) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedDictionary,
					"Document /Resources dictionaries exceed maximum memory budget");
			}
			dict_bytes += cat_bytes;
			replaced_resources.set(cat_key, std::move(resolved_cat));
		}
		total_retained_resources_bytes += dict_bytes;
		return std::make_shared<const PdfDict>(std::move(replaced_resources));
	};

	auto merge_resources_attr = [&](
									const PdfDict& node_dict,
									std::shared_ptr<const PdfDict>& inout_resources) -> PdfResult<void> {
		const PdfValue* res_val = node_dict.get("Resources");
		if (res_val == nullptr || res_val->is_null()) {
			return {};
		}
		if (const auto res_ref = res_val->as_ref()) {
			std::vector<IndirectRef> hop_refs;
			hop_refs.reserve(kMaxIndirectRefDepth);
			std::shared_ptr<const PdfDict> built_res;
			RECTO_PDF_TRY(
				CachedChainResult res_chain,
				resolve_indirect_chain_with_cache(
					*res_ref,
					parsed_resources_obj_cache,
					hop_refs,
					built_res));
			if (!res_chain.hit_cache) {
				if (!res_chain.terminal_value.is_null()) {
					const PdfDict* res_dict = res_chain.terminal_value.as_dict();
					if (res_dict == nullptr) {
						return make_doc_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedDictionary,
							"/Resources entry is not a PDF dictionary");
					}
					RECTO_PDF_TRY(
						std::shared_ptr<const PdfDict> fresh_res,
						build_resolved_resources_dict(*res_dict));
					built_res = std::move(fresh_res);
				}
			}
			RECTO_PDF_RETURN_IF_ERROR(insert_cached_chain_entries(
				parsed_resources_obj_cache,
				hop_refs,
				res_chain.hit_remaining_hops,
				built_res,
				total_retained_resources_bytes,
				kMaxTotalResourcesBytes,
				total_cached_resource_objs,
				kMaxCachedResourceObjs,
				"/Resources alias cache exceeds maximum memory budget"));
			if (built_res != nullptr) {
				inout_resources = std::move(built_res);
			}
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
		RECTO_PDF_TRY(
			std::shared_ptr<const PdfDict> built_res,
			build_resolved_resources_dict(*res_dict));
		inout_resources = std::move(built_res);
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
		std::vector<IndirectRef> node_hop_refs;
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
			node_hop_refs.reserve(kMaxIndirectRefDepth);
			RECTO_PDF_TRY(PdfValue val, resolve_object_with_visited(*ref, node_hop_refs));
			for (size_t h = 1u; h < node_hop_refs.size(); ++h) {
				if (!visited_page_nodes.insert(static_cast<int>(node_hop_refs[h].obj_num)).second) {
					return make_doc_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedObject,
						"Cyclic /Pages /Kids tree detected",
						std::nullopt,
						node_hop_refs[h].obj_num,
						node_hop_refs[h].gen_num);
				}
			}
			if (!node_hop_refs.empty()) {
				current_ref = node_hop_refs.back();
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
		// BleedBox, TrimBox and ArtBox belong to the page alone (ISO 32000-1 Table 30 lists only Resources, MediaBox,
		// CropBox and Rotate as inheritable): a node's own value is read, and an ancestor's never reaches a page.
		current_attrs.bleed_box = std::nullopt;
		current_attrs.trim_box = std::nullopt;
		current_attrs.art_box = std::nullopt;
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

			for (const IndirectRef& hr : node_hop_refs) {
				if (hr.obj_num != 0u) {
					page_obj_to_index_.emplace(make_ref_cache_key(hr), page_idx);
				}
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
	constexpr size_t kMaxMetadataFieldBytes = 64u * 1024u;
	constexpr size_t kMaxMetadataStreamBytes = 4u * 1024u * 1024u;
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
			if (s->span().size() > kMaxMetadataFieldBytes) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedDictionary,
					"/Info metadata string exceeds maximum field length");
			}
			RECTO_PDF_TRY(std::string utf8, pdf_string_to_utf8(s->span()));
			if (utf8.size() > kMaxMetadataFieldBytes) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedDictionary,
					"/Info metadata UTF-8 string exceeds maximum field length");
			}
			out_utf8 = std::move(utf8);
		} else if (const auto name_v = resolved.as_name()) {
			if (name_v->size() > kMaxMetadataFieldBytes) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedDictionary,
					"/Info metadata name exceeds maximum field length");
			}
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
				auto meta_bytes_res = decode_stream(*meta_stm, kMaxMetadataStreamBytes);
				if (!meta_bytes_res.has_value()) {
					if (meta_bytes_res.error().code == ErrorCode::DecompressionLimitExceeded ||
						meta_bytes_res.error().code == ErrorCode::OutOfMemory) {
						return meta_bytes_res.error();
					}
				} else {
					const std::string_view xmp_xml(
						reinterpret_cast<const char*>(meta_bytes_res->data()),
						meta_bytes_res->size());
					ExtractedXmpPacket xmp_pkt = extract_xmp_metadata_packet(
						xmp_xml,
						&xmp_entity_scan_steps_);
					auto assign_xmp_fallback = [&](
												   std::string& field,
												   std::string&& extracted) -> PdfResult<void> {
						if (!field.empty() || extracted.empty()) {
							return {};
						}
						if (extracted.size() > kMaxMetadataFieldBytes) {
							return make_doc_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedDictionary,
								"XMP metadata field exceeds maximum field length");
						}
						field = std::move(extracted);
						return {};
					};
					RECTO_PDF_RETURN_IF_ERROR(assign_xmp_fallback(description_.metadata.title, std::move(xmp_pkt.title)));
					RECTO_PDF_RETURN_IF_ERROR(assign_xmp_fallback(description_.metadata.author, std::move(xmp_pkt.author)));
					RECTO_PDF_RETURN_IF_ERROR(assign_xmp_fallback(description_.metadata.subject, std::move(xmp_pkt.subject)));
					RECTO_PDF_RETURN_IF_ERROR(assign_xmp_fallback(description_.metadata.keywords, std::move(xmp_pkt.pdf_keywords)));
					RECTO_PDF_RETURN_IF_ERROR(assign_xmp_fallback(description_.metadata.keywords, std::move(xmp_pkt.dc_subject)));
					RECTO_PDF_RETURN_IF_ERROR(assign_xmp_fallback(description_.metadata.creator, std::move(xmp_pkt.creator_tool)));
					RECTO_PDF_RETURN_IF_ERROR(assign_xmp_fallback(description_.metadata.producer, std::move(xmp_pkt.producer)));
					RECTO_PDF_RETURN_IF_ERROR(assign_xmp_fallback(description_.metadata.creation_date, std::move(xmp_pkt.create_date)));
					RECTO_PDF_RETURN_IF_ERROR(assign_xmp_fallback(description_.metadata.mod_date, std::move(xmp_pkt.modify_date)));
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
	static_assert(std::is_trivially_copyable_v<ParsedDest>);
	auto parse_dest_array = [this](const PdfArray& arr) -> ParsedDest {
		ParsedDest out{};
		if (arr.empty()) {
			return out;
		}
		if (const auto page_ref = arr[0].as_ref()) {
			const auto it = page_obj_to_index_.find(make_ref_cache_key(*page_ref));
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

		if (arr.size() < 2u) {
			out.fit_mode = "Fit";
			out.valid = true;
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
			out.fit_mode = "XYZ";
			if (const auto left = get_num_arg(2u)) {
				out.x_pt = *left;
			}
			if (const auto top = get_num_arg(3u)) {
				out.y_pt = *top;
			}
			if (const auto zm = get_num_arg(4u)) {
				out.zoom = *zm;
			}
		} else if (*fit_name == "Fit") {
			out.fit_mode = "Fit";
		} else if (*fit_name == "FitB") {
			out.fit_mode = "FitB";
		} else if (*fit_name == "FitH" || *fit_name == "FitBH") {
			out.fit_mode = (*fit_name == "FitH") ? "FitH" : "FitBH";
			if (const auto top = get_num_arg(2u)) {
				out.y_pt = *top;
			}
		} else if (*fit_name == "FitV" || *fit_name == "FitBV") {
			out.fit_mode = (*fit_name == "FitV") ? "FitV" : "FitBV";
			if (const auto left = get_num_arg(2u)) {
				out.x_pt = *left;
			}
		} else if (*fit_name == "FitR") {
			out.fit_mode = "FitR";
			if (const auto left = get_num_arg(2u)) {
				out.x_pt = *left;
			}
			if (const auto top = get_num_arg(5u)) {
				out.y_pt = *top;
			}
		} else {
			return out;
		}

		out.valid = true;
		return out;
	};

	constexpr size_t kMaxDestNameBytes = 4096u;
	constexpr size_t kMaxTotalNamedDestBytes = 16u * 1024u * 1024u;
	constexpr size_t kMaxCachedDestObjs = 65'536u;
	size_t total_named_dest_bytes = 0u;
	size_t total_cached_dest_objs = 0u;

	struct CachedDestEntry {
		ParsedDest value{};
		size_t remaining_hops{1u};
	};
	std::unordered_map<uint64_t, CachedDestEntry> parsed_dest_array_obj_cache;
	std::unordered_map<uint64_t, CachedDestEntry> parsed_named_dest_obj_cache;
	std::unordered_map<uint64_t, CachedDestEntry> parsed_resolved_dest_obj_cache;

	auto parse_direct_dest_target = [&](const PdfValue& dval) -> PdfResult<ParsedDest> {
		if (const PdfArray* arr = dval.as_array()) {
			return parse_dest_array(*arr);
		}
		if (const PdfDict* ddict = dval.as_dict()) {
			if (const PdfValue* inner_d = ddict->get("D")) {
				if (const auto inner_ref = inner_d->as_ref()) {
					std::vector<IndirectRef> hop_refs;
					hop_refs.reserve(kMaxIndirectRefDepth);
					ParsedDest pd{};
					auto inner_chain = resolve_indirect_chain_with_cache(
						*inner_ref,
						parsed_dest_array_obj_cache,
						hop_refs,
						pd);
					if (inner_chain.has_value()) {
						if (!inner_chain->hit_cache) {
							if (const PdfArray* arr = inner_chain->terminal_value.as_array()) {
								pd = parse_dest_array(*arr);
							}
						}
						RECTO_PDF_RETURN_IF_ERROR(insert_cached_chain_entries(
							parsed_dest_array_obj_cache,
							hop_refs,
							inner_chain->hit_remaining_hops,
							pd,
							total_named_dest_bytes,
							kMaxTotalNamedDestBytes,
							total_cached_dest_objs,
							kMaxCachedDestObjs,
							"Destination alias cache exceeds maximum entry or memory budget"));
					} else if (inner_chain.error().code == ErrorCode::OutOfMemory ||
							   inner_chain.error().code == ErrorCode::DecompressionLimitExceeded) {
						return inner_chain.error();
					}
					return pd;
				}
				auto inner_res = resolve_value(*inner_d);
				if (inner_res.has_value()) {
					if (const PdfArray* arr = inner_res->as_array()) {
						return parse_dest_array(*arr);
					}
				} else if (inner_res.error().code == ErrorCode::OutOfMemory ||
						   inner_res.error().code == ErrorCode::DecompressionLimitExceeded) {
					return inner_res.error();
				}
			}
		}
		return ParsedDest{};
	};

	auto parse_dest_target_value = [&](const PdfValue& raw_v) -> PdfResult<ParsedDest> {
		if (const auto ref = raw_v.as_ref()) {
			std::vector<IndirectRef> hop_refs;
			hop_refs.reserve(kMaxIndirectRefDepth);
			ParsedDest pd{};
			auto dest_chain = resolve_indirect_chain_with_cache(
				*ref,
				parsed_named_dest_obj_cache,
				hop_refs,
				pd);
			if (dest_chain.has_value()) {
				if (!dest_chain->hit_cache) {
					RECTO_PDF_TRY(
						ParsedDest direct_pd,
						parse_direct_dest_target(dest_chain->terminal_value));
					pd = direct_pd;
				}
				RECTO_PDF_RETURN_IF_ERROR(insert_cached_chain_entries(
					parsed_named_dest_obj_cache,
					hop_refs,
					dest_chain->hit_remaining_hops,
					pd,
					total_named_dest_bytes,
					kMaxTotalNamedDestBytes,
					total_cached_dest_objs,
					kMaxCachedDestObjs,
					"Destination alias cache exceeds maximum entry or memory budget"));
			} else if (dest_chain.error().code == ErrorCode::OutOfMemory ||
					   dest_chain.error().code == ErrorCode::DecompressionLimitExceeded) {
				return dest_chain.error();
			}
			return pd;
		}
		auto resolved_res = resolve_value(raw_v);
		if (!resolved_res.has_value()) {
			if (resolved_res.error().code == ErrorCode::OutOfMemory ||
				resolved_res.error().code == ErrorCode::DecompressionLimitExceeded) {
				return resolved_res.error();
			}
			return ParsedDest{};
		}
		return parse_direct_dest_target(*resolved_res);
	};

	// Names (Catalog /Dests, PDF 1.1) and strings (the /Names /Dests name tree) are separate namespaces (ISO 32000-1
	// 12.3.2.3): a destination given as a name is looked up only among names, one given as a string only among
	// strings. One map holds both, each key led by its space, '/' for a name and '(' for a string.
	auto record_named_dest = [&](char space, std::string&& name, const PdfValue& target_val) -> PdfResult<void> {
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
		RECTO_PDF_TRY(ParsedDest pd, parse_dest_target_value(target_val));
		if (!pd.valid) {
			return {};
		}
		if (total_named_dest_bytes + entry_bytes > kMaxTotalNamedDestBytes) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"Named destinations exceed maximum entry or memory budget");
		}
		name.insert(name.begin(), space);
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
					RECTO_PDF_RETURN_IF_ERROR(record_named_dest('/', std::string(k.view()), v));
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
					std::unordered_set<uint64_t> visited_name_nodes;
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
							if (!visited_name_nodes.insert(make_ref_cache_key(*ref)).second) {
								return {};
							}
							std::vector<IndirectRef> hop_refs;
							hop_refs.reserve(kMaxIndirectRefDepth);
							auto resolved_res = resolve_object_with_visited(*ref, hop_refs);
							if (!resolved_res.has_value()) {
								return {};
							}
							for (size_t h = 1u; h < hop_refs.size(); ++h) {
								if (!visited_name_nodes.insert(make_ref_cache_key(hop_refs[h])).second) {
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
												record_named_dest('(', std::move(dest_name), (*n_arr)[i + 1u]));
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

	auto resolve_dest_value = [&](const PdfValue& raw_dest) -> PdfResult<ParsedDest> {
		auto parse_resolved_dest_val = [&](const PdfValue& dval) -> PdfResult<ParsedDest> {
			if (dval.is_array() || dval.is_dict()) {
				return parse_direct_dest_target(dval);
			}
			std::string name_key;
			if (const auto nm = dval.as_name()) {
				name_key = "/" + std::string(*nm);
			} else if (const PdfString* s = dval.as_string()) {
				auto utf8_res = pdf_string_to_utf8(s->span());
				name_key = "(" + (utf8_res.has_value() ? std::move(*utf8_res) : std::string(s->as_byte_view()));
			}
			if (name_key.size() > 1u) {
				const auto it = named_dests_.find(name_key);
				if (it != named_dests_.end()) {
					return it->second;
				}
			}
			return ParsedDest{};
		};

		if (const auto ref = raw_dest.as_ref()) {
			std::vector<IndirectRef> hop_refs;
			hop_refs.reserve(kMaxIndirectRefDepth);
			ParsedDest pd{};
			auto dest_chain = resolve_indirect_chain_with_cache(
				*ref,
				parsed_resolved_dest_obj_cache,
				hop_refs,
				pd);
			if (!dest_chain.has_value()) {
				if (dest_chain.error().code == ErrorCode::OutOfMemory ||
					dest_chain.error().code == ErrorCode::DecompressionLimitExceeded) {
					return dest_chain.error();
				}
				return ParsedDest{};
			}
			if (!dest_chain->hit_cache) {
				RECTO_PDF_TRY(
					ParsedDest parsed_pd,
					parse_resolved_dest_val(dest_chain->terminal_value));
				pd = parsed_pd;
			}
			RECTO_PDF_RETURN_IF_ERROR(insert_cached_chain_entries(
				parsed_resolved_dest_obj_cache,
				hop_refs,
				dest_chain->hit_remaining_hops,
				pd,
				total_named_dest_bytes,
				kMaxTotalNamedDestBytes,
				total_cached_dest_objs,
				kMaxCachedDestObjs,
				"Destination alias cache exceeds maximum entry or memory budget"));
			return pd;
		}
		auto resolved_res = resolve_value(raw_dest);
		if (!resolved_res.has_value()) {
			if (resolved_res.error().code == ErrorCode::OutOfMemory ||
				resolved_res.error().code == ErrorCode::DecompressionLimitExceeded) {
				return resolved_res.error();
			}
			return ParsedDest{};
		}
		return parse_resolved_dest_val(*resolved_res);
	};

	// 5. Resolve `/Catalog /Outlines` tree with cycle guard on `/First`/`/Next`:
	if (const PdfValue* outlines_val = catalog_.get("Outlines");
		outlines_val != nullptr && !outlines_val->is_null()) {
		auto outlines_res = resolve_value(*outlines_val);
		if (outlines_res.has_value()) {
			if (const PdfDict* outlines_root = outlines_res->as_dict()) {
				std::unordered_set<uint64_t> visited_outline_objs;
				if (const auto root_out_ref = outlines_val->as_ref()) {
					visited_outline_objs.insert(make_ref_cache_key(*root_out_ref));
				}
				size_t total_outline_nodes = 0;
				size_t total_outline_bytes = 0;
				constexpr size_t kMaxOutlineStringBytes = 4096u;
				constexpr size_t kMaxTotalOutlineBytes = 8u * 1024u * 1024u;

				auto build_outline_siblings = [&](auto& self,
												  const PdfValue* first_val,
												  std::vector<OutlineNode>& out_siblings,
												  size_t depth) -> PdfResult<void> {
					if (first_val == nullptr || first_val->is_null()) {
						return {};
					}
					if (depth > kMaxOutlineDepth) {
						return make_doc_error(
							ErrorCode::RecursionLimitExceeded,
							DiagnosticId::RecursionLimitExceeded,
							"/Outlines tree exceeds maximum depth");
					}

					PdfValue current_ptr;
					RECTO_PDF_TRY(PdfValue cloned_first, first_val->deep_clone());
					current_ptr = std::move(cloned_first);

					while (!current_ptr.is_null()) {
						if (total_outline_nodes >= kMaxOutlineNodes) {
							return make_doc_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedDictionary,
								"/Outlines tree exceeds maximum node budget");
						}
						PdfValue item_val;
						if (const auto item_ref = current_ptr.as_ref()) {
							if (!visited_outline_objs.insert(make_ref_cache_key(*item_ref)).second) {
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
								if (!visited_outline_objs.insert(make_ref_cache_key(hop_refs[h])).second) {
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
									if (ts->span().size() > kMaxOutlineStringBytes) {
										return make_doc_error(
											ErrorCode::DamagedFormat,
											DiagnosticId::DamagedDictionary,
											"/Outlines item /Title exceeds maximum length");
									}
									auto utf8_res = pdf_string_to_utf8(ts->span());
									if (utf8_res.has_value()) {
										if (utf8_res->size() > kMaxOutlineStringBytes) {
											return make_doc_error(
												ErrorCode::DamagedFormat,
												DiagnosticId::DamagedDictionary,
												"/Outlines item UTF-8 /Title exceeds maximum length");
										}
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
							RECTO_PDF_TRY(const ParsedDest pd, resolve_dest_value(*dest_v));
							if (pd.valid) {
								node.page_index = pd.page_index;
								node.dest_x_pt = pd.x_pt;
								node.dest_y_pt = pd.y_pt;
								node.zoom = pd.zoom;
								node.fit_mode = std::string(pd.fit_mode);
								node.has_destination = true;
							}
						} else if (const PdfValue* act_v = item_dict->get("A")) {
							auto act_res = resolve_value(*act_v);
							if (act_res.has_value()) {
								if (const PdfDict* act_dict = act_res->as_dict()) {
									const auto s_name = act_dict->get_name("S").value_or("");
									if (s_name == "GoTo") {
										if (const PdfValue* d_v = act_dict->get("D")) {
											RECTO_PDF_TRY(const ParsedDest pd, resolve_dest_value(*d_v));
											if (pd.valid) {
												node.page_index = pd.page_index;
												node.dest_x_pt = pd.x_pt;
												node.dest_y_pt = pd.y_pt;
												node.zoom = pd.zoom;
												node.fit_mode = std::string(pd.fit_mode);
												node.has_destination = true;
											}
										}
									} else if (s_name == "URI") {
										if (const PdfValue* uri_v = act_dict->get("URI")) {
											auto uri_res = resolve_value(*uri_v);
											if (uri_res.has_value()) {
												if (const PdfString* us = uri_res->as_string()) {
													if (us->span().size() > kMaxOutlineStringBytes) {
														return make_doc_error(
															ErrorCode::DamagedFormat,
															DiagnosticId::DamagedDictionary,
															"/Outlines item /URI exceeds maximum length");
													}
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

						const size_t node_bytes =
							sizeof(OutlineNode) + node.title.size() + node.uri.size() + node.fit_mode.size();
						if (total_outline_bytes + node_bytes > kMaxTotalOutlineBytes) {
							return make_doc_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedDictionary,
								"/Outlines tree exceeds aggregate memory budget");
						}
						total_outline_bytes += node_bytes;

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
											 std::optional<uint64_t> root_ref_key = std::nullopt) -> bool {
		constexpr size_t kMaxActionChainDepth = 64u;
		constexpr size_t kMaxActionChainNodes = 256u;

		struct ActionFrame {
			PdfValue value{};
			size_t depth{0};
			bool from_array{false};
		};

		std::unordered_set<uint64_t> visited_actions;
		if (root_ref_key.has_value()) {
			visited_actions.insert(*root_ref_key);
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
				if (us == nullptr || us->span().size() > 8192u) {
					return true;
				}
				auto u_utf8 = pdf_string_to_utf8(us->span());
				if (!u_utf8.has_value() || u_utf8->size() > 8192u || !is_safe_external_uri(*u_utf8)) {
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
			std::vector<IndirectRef> ref_hop_list;
			ref_hop_list.reserve(kMaxIndirectRefDepth);
			while (const auto ref = cur_val.as_ref()) {
				if (!visited_actions.insert(make_ref_cache_key(*ref)).second) {
					already_visited = true;
					break;
				}
				if (ref_hop_list.size() >= kMaxIndirectRefDepth) {
					return true;
				}
				ref_hop_list.push_back(*ref);
				auto step_res = resolve_single_indirect_hop(*ref, ref_hop_list);
				if (!step_res.has_value()) {
					return true;
				}
				if (step_res->next_ref.has_value()) {
					cur_val = PdfValue(*step_res->next_ref);
					continue;
				}
				cur_val = std::move(step_res->terminal_value);
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
	constexpr size_t kMaxTotalDocumentLinks = 65'536u;
	constexpr size_t kMaxLinkUriBytes = 8192u;
	constexpr size_t kMaxLinkActionTypeBytes = 256u;
	constexpr size_t kMaxTotalLinkBytes = 2u * 1024u * 1024u;
	size_t total_document_links = 0u;
	size_t total_link_bytes = 0u;
	size_t total_action_cache_bytes = 0u;
	size_t total_cached_action_objs = 0u;

	struct CachedActionSummary {
		LinkKind kind{LinkKind::RefusedAction};
		int32_t page_index{-1};
		double x_pt{0.0};
		double y_pt{0.0};
		double zoom{0.0};
		std::string uri{};
		std::string action_type{};
		bool is_inert{true};
	};
	struct CachedActionEntry {
		std::shared_ptr<const CachedActionSummary> value{};
		size_t remaining_hops{1u};
	};
	std::unordered_map<uint64_t, CachedActionEntry> parsed_action_obj_cache;
	constexpr size_t kMaxCachedActionObjs = 65'536u;

	auto parse_action_dict_summary = [&](
										 const PdfDict& act_dict,
										 std::optional<uint64_t> act_ref_key) -> PdfResult<CachedActionSummary> {
		CachedActionSummary out{};
		std::string s_type;
		if (const auto s_name = act_dict.get_name("S")) {
			if (s_name->size() > kMaxLinkActionTypeBytes) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedDictionary,
					"/Link action /S name exceeds maximum length");
			}
			s_type = std::string(*s_name);
		}
		out.action_type = s_type;

		if (s_type == "URI") {
			if (const PdfValue* uri_val = act_dict.get("URI")) {
				auto uri_res = resolve_value(*uri_val);
				if (uri_res.has_value()) {
					if (const PdfString* us = uri_res->as_string()) {
						if (us->span().size() > kMaxLinkUriBytes) {
							return make_doc_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedDictionary,
								"/Link /URI string exceeds maximum length");
						}
						auto u_utf8 = pdf_string_to_utf8(us->span());
						if (u_utf8.has_value()) {
							if (u_utf8->size() > kMaxLinkUriBytes) {
								return make_doc_error(
									ErrorCode::DamagedFormat,
									DiagnosticId::DamagedDictionary,
									"/Link /URI UTF-8 string exceeds maximum length");
							}
							out.uri = std::string(trim_ascii_whitespace(*u_utf8));
						}
					}
				}
			}
		}

		if (action_chain_contains_refused(act_dict, act_ref_key)) {
			out.kind = LinkKind::RefusedAction;
			out.is_inert = true;
			out.uri.clear();
			return out;
		}
		if (s_type == "GoTo") {
			if (const PdfValue* d_val = act_dict.get("D")) {
				RECTO_PDF_TRY(const ParsedDest pd, resolve_dest_value(*d_val));
				if (pd.valid) {
					out.kind = LinkKind::Internal;
					out.is_inert = false;
					out.page_index = pd.page_index;
					out.x_pt = pd.x_pt;
					out.y_pt = pd.y_pt;
					out.zoom = pd.zoom;
				}
			}
		} else if (s_type == "URI") {
			if (!out.uri.empty() && is_safe_external_uri(out.uri)) {
				out.kind = LinkKind::ExternalUri;
				out.is_inert = false;
			} else {
				out.kind = LinkKind::RefusedAction;
				out.is_inert = true;
			}
		} else if (s_type == "GoToR" || s_type == "GoToE") {
			out.kind = LinkKind::RemoteGoTo;
			out.is_inert = false;
			if (const PdfValue* f_val = act_dict.get("F")) {
				auto f_res = resolve_value(*f_val);
				if (f_res.has_value()) {
					const PdfString* target_fs = f_res->as_string();
					if (target_fs == nullptr) {
						if (const PdfDict* fdict = f_res->as_dict()) {
							target_fs = fdict->get_string("UF");
							if (target_fs == nullptr) {
								target_fs = fdict->get_string("F");
							}
						}
					}
					if (target_fs != nullptr) {
						if (target_fs->span().size() > kMaxLinkUriBytes) {
							return make_doc_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedDictionary,
								"/Link remote file specification exceeds maximum length");
						}
						auto f_utf8 = pdf_string_to_utf8(target_fs->span());
						if (f_utf8.has_value()) {
							if (f_utf8->size() > kMaxLinkUriBytes) {
								return make_doc_error(
									ErrorCode::DamagedFormat,
									DiagnosticId::DamagedDictionary,
									"/Link remote UTF-8 file specification exceeds maximum length");
							}
							out.uri = std::move(*f_utf8);
						}
					}
				}
			}
			if (const PdfValue* d_val = act_dict.get("D")) {
				auto d_res = resolve_value(*d_val);
				if (d_res.has_value()) {
					if (const PdfArray* d_arr = d_res->as_array();
						d_arr != nullptr && !d_arr->empty()) {
						if (const auto p_int = (*d_arr)[0].as_int();
							p_int.has_value() && *p_int >= 0) {
							out.page_index = static_cast<int32_t>(*p_int);
						}
					}
				}
			}
		} else if (s_type == "Named") {
			if (const auto n_name = act_dict.get_name("N")) {
				if (n_name->size() > kMaxLinkUriBytes) {
					return make_doc_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedDictionary,
						"/Link /Named action name exceeds maximum length");
				}
				out.kind = LinkKind::NamedAction;
				out.is_inert = false;
				out.uri = std::string(*n_name);
			}
		} else {
			// `/Launch`, `/JavaScript`, or any other unknown action is inert.
			out.kind = LinkKind::RefusedAction;
			out.is_inert = true;
		}
		return out;
	};

	auto record_page_link = [&](ResolvedPageRecord& page_rec, PageLink&& link) -> PdfResult<void> {
		const size_t link_bytes =
			sizeof(PageLink) + link.uri.size() + link.action_type.size();
		if (total_document_links >= kMaxTotalDocumentLinks ||
			total_link_bytes + link_bytes > kMaxTotalLinkBytes) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"Document /Annots /Link entries exceed maximum count or memory budget");
		}
		++total_document_links;
		total_link_bytes += link_bytes;
		page_rec.links.push_back(std::move(link));
		return {};
	};

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

		std::unordered_set<uint64_t> visited_page_annots;
		for (const PdfValue& annot_elem : *annots_arr) {
			if (annot_elem.is_null()) {
				continue;
			}
			PdfValue annot_val;
			if (const auto annot_ref = annot_elem.as_ref()) {
				if (!visited_page_annots.insert(make_ref_cache_key(*annot_ref)).second) {
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
					if (!visited_page_annots.insert(make_ref_cache_key(hop_refs[h])).second) {
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
				annot_val = std::move(annot_res.value());
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
									sub_ref ? std::optional<uint64_t>(make_ref_cache_key(*sub_ref)) : std::nullopt)) {
								aa_has_refused = true;
								const std::string_view sub_s =
									sub_dict->get_name("S").value_or("AdditionalAction");
								link.action_type = std::string(
									sub_s.substr(0u, std::min(sub_s.size(), kMaxLinkActionTypeBytes)));
								break;
							}
						}
					}
				}
			}

			if (aa_has_refused) {
				link.kind = LinkKind::RefusedAction;
				link.is_inert = true;
				RECTO_PDF_RETURN_IF_ERROR(record_page_link(page_rec, std::move(link)));
				continue;
			}

			if (const PdfValue* act_val = annot_dict->get("A");
				act_val != nullptr && !act_val->is_null()) {
				std::shared_ptr<const CachedActionSummary> act_summary;
				std::optional<CachedActionSummary> inline_summary;
				if (const auto act_ref = act_val->as_ref()) {
					std::vector<IndirectRef> hop_refs;
					hop_refs.reserve(kMaxIndirectRefDepth);
					auto act_chain_res = resolve_indirect_chain_with_cache(
						*act_ref,
						parsed_action_obj_cache,
						hop_refs,
						act_summary);
					if (!act_chain_res.has_value()) {
						if (act_chain_res.error().code == ErrorCode::OutOfMemory ||
							act_chain_res.error().code == ErrorCode::DecompressionLimitExceeded) {
							return act_chain_res.error();
						}
					} else {
						if (!act_chain_res->hit_cache) {
							if (const PdfDict* act_dict = act_chain_res->terminal_value.as_dict()) {
								const IndirectRef target_ref = hop_refs.empty()
									? *act_ref
									: hop_refs.back();
								RECTO_PDF_TRY(
									CachedActionSummary parsed_sum,
									parse_action_dict_summary(*act_dict, make_ref_cache_key(target_ref)));
								const size_t summary_bytes = sizeof(CachedActionSummary) +
									parsed_sum.uri.size() + parsed_sum.action_type.size();
								if (total_action_cache_bytes + summary_bytes > kMaxTotalLinkBytes) {
									return make_doc_error(
										ErrorCode::DamagedFormat,
										DiagnosticId::DamagedDictionary,
										"Document /Annots /Link action cache exceeds memory budget");
								}
								total_action_cache_bytes += summary_bytes;
								act_summary = std::make_shared<const CachedActionSummary>(
									std::move(parsed_sum));
							}
						}
						RECTO_PDF_RETURN_IF_ERROR(insert_cached_chain_entries(
							parsed_action_obj_cache,
							hop_refs,
							act_chain_res->hit_remaining_hops,
							act_summary,
							total_action_cache_bytes,
							kMaxTotalLinkBytes,
							total_cached_action_objs,
							kMaxCachedActionObjs,
							"Document /Annots /Link action alias cache exceeds budget"));
					}
				} else {
					auto act_res = resolve_value(*act_val);
					if (act_res.has_value()) {
						if (const PdfDict* act_dict = act_res->as_dict()) {
							RECTO_PDF_TRY(
								CachedActionSummary parsed_sum,
								parse_action_dict_summary(*act_dict, std::nullopt));
							inline_summary = std::move(parsed_sum);
						}
					}
				}

				const CachedActionSummary* summary_ptr = act_summary != nullptr
					? act_summary.get()
					: (inline_summary.has_value() ? &(*inline_summary) : nullptr);
				if (summary_ptr != nullptr) {
					link.kind = summary_ptr->kind;
					link.is_inert = summary_ptr->is_inert;
					link.page_index = summary_ptr->page_index;
					link.x_pt = summary_ptr->x_pt;
					link.y_pt = summary_ptr->y_pt;
					link.dest_y_pt = summary_ptr->y_pt;
					link.zoom = summary_ptr->zoom;
					link.uri = summary_ptr->uri;
					link.action_type = summary_ptr->action_type;
				}
			} else if (const PdfValue* dest_val = annot_dict->get("Dest");
					   dest_val != nullptr && !dest_val->is_null()) {
				RECTO_PDF_TRY(const ParsedDest pd, resolve_dest_value(*dest_val));
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

			RECTO_PDF_RETURN_IF_ERROR(record_page_link(page_rec, std::move(link)));
		}
	}

	// 7. Enumerate and deduplicate `/Font` descriptors across pages, Form
	//    XObjects, and `/Catalog /AcroForm /DR`:
	std::unordered_set<uint64_t> visited_font_objs;
	std::unordered_set<uint64_t> visited_xobj_objs;
	constexpr size_t kMaxFontStringBytes = 1024u;
	constexpr size_t kMaxTotalFonts = 4096u;
	constexpr size_t kMaxTotalFontBytes = 2u * 1024u * 1024u;
	size_t total_font_bytes = 0u;

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

	auto inspect_font_dict = [&](const PdfDict& font_dict, std::string_view fallback_key) -> PdfResult<void> {
		FontSummary summary{};
		const std::string_view subtype_sv = font_dict.get_name("Subtype").value_or("");
		if (subtype_sv.size() > kMaxFontStringBytes) {
			return make_doc_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"/Font /Subtype name exceeds maximum length");
		}
		summary.subtype = std::string(subtype_sv);

		if (const auto bf_name = font_dict.get_name("BaseFont")) {
			if (bf_name->size() > kMaxFontStringBytes) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedDictionary,
					"/Font /BaseFont name exceeds maximum length");
			}
			summary.name = std::string(*bf_name);
		} else if (const PdfString* bf_str = font_dict.get_string("BaseFont")) {
			if (bf_str->span().size() > kMaxFontStringBytes) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedDictionary,
					"/Font /BaseFont string exceeds maximum length");
			}
			auto utf8_res = pdf_string_to_utf8(bf_str->span());
			summary.name = utf8_res.has_value()
				? std::move(*utf8_res)
				: std::string(bf_str->as_byte_view());
		} else if (const auto nm = font_dict.get_name("Name")) {
			if (nm->size() > kMaxFontStringBytes) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedDictionary,
					"/Font /Name exceeds maximum length");
			}
			summary.name = std::string(*nm);
		} else if (!fallback_key.empty()) {
			if (fallback_key.size() > kMaxFontStringBytes) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedDictionary,
					"/Font resource key exceeds maximum length");
			}
			summary.name = std::string(fallback_key);
		}

		// Extract `/Encoding`:
		if (const PdfValue* enc_val = font_dict.get("Encoding");
			enc_val != nullptr && !enc_val->is_null()) {
			auto enc_res = resolve_value(*enc_val);
			if (enc_res.has_value()) {
				if (const auto enc_name = enc_res->as_name()) {
					if (enc_name->size() > kMaxFontStringBytes) {
						return make_doc_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedDictionary,
							"/Font /Encoding name exceeds maximum length");
					}
					summary.encoding = std::string(*enc_name);
				} else if (const PdfDict* enc_dict = enc_res->as_dict()) {
					summary.has_differences = enc_dict->contains("Differences");
					if (const auto base_enc = enc_dict->get_name("BaseEncoding")) {
						if (base_enc->size() > kMaxFontStringBytes) {
							return make_doc_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedDictionary,
								"/Font /BaseEncoding name exceeds maximum length");
							}
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
									if (cid_bf->size() > kMaxFontStringBytes) {
										return make_doc_error(
											ErrorCode::DamagedFormat,
											DiagnosticId::DamagedDictionary,
											"/CIDFont /BaseFont name exceeds maximum length");
									}
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
			const size_t entry_bytes = sizeof(FontSummary) +
				summary.name.size() + summary.base_font.size() +
				summary.clean_name.size() + summary.subtype.size() +
				summary.encoding.size();
			if (description_.fonts.size() >= kMaxTotalFonts ||
				total_font_bytes + entry_bytes > kMaxTotalFontBytes) {
				return make_doc_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedDictionary,
					"Document font inventory exceeds maximum count or memory budget");
			}
			total_font_bytes += entry_bytes;
			description_.fonts.push_back(std::move(summary));
		} else {
			dup_it->has_to_unicode = dup_it->has_to_unicode || summary.has_to_unicode;
			dup_it->has_differences = dup_it->has_differences || summary.has_differences;
		}
		return {};
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
							if (!visited_font_objs.insert(make_ref_cache_key(*f_ref)).second) {
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
								if (!visited_font_objs.insert(make_ref_cache_key(hop_refs[h])).second) {
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
							RECTO_PDF_RETURN_IF_ERROR(inspect_font_dict(*f_dict, res_key.view()));
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
							if (!visited_xobj_objs.insert(make_ref_cache_key(*x_ref)).second) {
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
								if (!visited_xobj_objs.insert(make_ref_cache_key(hop_refs[h])).second) {
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

	structure_loaded_ = true;
	structure_committed = true;
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
