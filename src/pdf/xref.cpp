// PDF cross-reference table and stream parser (classic `xref` tables, PDF 1.5+
// `/Type /XRef` streams, `/Type /ObjStm` compressed object streams, hybrid
// `/XRefStm`), trailer merger, incremental `/Prev` chain follower,
// damaged-file recovery scanner, and cycle-guarded indirect object resolver
// (ISO 32000-1 sections 7.5.4, 7.5.6, 7.5.7, and 7.5.8; D01 T01 sections 5-6).
#include "xref.h"

#include "filters.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <tuple>
#include <new>
#include <unordered_set>
#include <utility>

namespace recto::pdf {

namespace {

constexpr std::string_view kPdfHeaderPrefix = "%PDF-";
constexpr std::string_view kStartxrefKeyword = "startxref";
constexpr std::string_view kEofMarker = "%%EOF";

[[nodiscard]] bool matches_token_at(
	std::span<const uint8_t> bytes,
	size_t pos,
	std::string_view token) noexcept
{
	if (pos > bytes.size() || bytes.size() - pos < token.size()) {
		return false;
	}
	if (std::memcmp(bytes.data() + pos, token.data(), token.size()) != 0) {
		return false;
	}
	if (pos > 0 && Lexer::is_pdf_regular(bytes[pos - 1])) {
		return false;
	}
	const size_t after = pos + token.size();
	if (after < bytes.size() && Lexer::is_pdf_regular(bytes[after])) {
		return false;
	}
	return true;
}

[[nodiscard]] bool is_on_comment_line(
	std::span<const uint8_t> bytes,
	size_t pos) noexcept
{
	size_t scan = pos;
	while (scan > 0) {
		--scan;
		const uint8_t b = bytes[scan];
		if (b == '\n' || b == '\r') {
			break;
		}
		if (b == '%') {
			return true;
		}
	}
	return false;
}

[[nodiscard]] size_t skip_pdf_whitespace_and_comments(
	std::span<const uint8_t> bytes,
	size_t p) noexcept
{
	while (p < bytes.size()) {
		if (Lexer::is_pdf_whitespace(bytes[p])) {
			++p;
			continue;
		}
		if (bytes[p] == '%') {
			while (p < bytes.size() && bytes[p] != '\n' && bytes[p] != '\r') {
				++p;
			}
			continue;
		}
		break;
	}
	return p;
}

[[nodiscard]] bool contains_eof_marker(std::span<const uint8_t> tail) noexcept
{
	if (tail.size() < kEofMarker.size()) {
		return false;
	}
	const size_t limit = tail.size() - kEofMarker.size();
	for (size_t i = 0; i <= limit; ++i) {
		if (std::memcmp(tail.data() + i, kEofMarker.data(), kEofMarker.size()) == 0) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool next_non_comment_token(
	Lexer& lexer,
	Token& out_tok,
	PdfError& out_err)
{
	while (true) {
		auto res = lexer.next_token();
		if (!res.has_value()) {
			out_err = std::move(res).error();
			return false;
		}
		if (res.value().kind == TokenKind::Comment) {
			continue;
		}
		out_tok = std::move(res).value();
		return true;
	}
}

[[nodiscard]] PdfResult<void> merge_trailer_from_revision(
	PdfDict& merged,
	const PdfDict& rev_trailer)
{
	constexpr std::string_view kMergedKeys[] = {
		"Root",
		"Info",
		"Encrypt",
		"ID",
		"Size",
	};
	for (const std::string_view key : kMergedKeys) {
		if (!merged.contains(key)) {
			if (const PdfValue* val = rev_trailer.get(key)) {
				RECTO_PDF_TRY(PdfValue cloned, val->deep_clone());
				merged.set(key, std::move(cloned));
			}
		}
	}
	return {};
}

[[nodiscard]] PdfResult<void> overwrite_trailer_from_revision(
	PdfDict& merged,
	const PdfDict& rev_trailer)
{
	constexpr std::string_view kMergedKeys[] = {
		"Root",
		"Info",
		"Encrypt",
		"ID",
		"Size",
	};
	for (const std::string_view key : kMergedKeys) {
		if (const PdfValue* val = rev_trailer.get(key)) {
			RECTO_PDF_TRY(PdfValue cloned, val->deep_clone());
			merged.set(key, std::move(cloned));
		}
	}
	return {};
}

[[nodiscard]] PdfError make_xref_error(
	ErrorCode code,
	DiagnosticId diag_id,
	std::string_view message,
	std::optional<uint64_t> byte_offset = std::nullopt,
	std::optional<uint32_t> object_num = std::nullopt,
	uint16_t generation_num = 0) noexcept
{
	record_diagnostic(code, diag_id, byte_offset, object_num, generation_num);
	return PdfError(code, message, byte_offset, object_num, generation_num);
}

[[nodiscard]] constexpr uint64_t read_big_endian_field(
	const uint8_t* ptr,
	size_t width) noexcept
{
	uint64_t val = 0;
	for (size_t i = 0; i < width; ++i) {
		val = (val << 8u) | static_cast<uint64_t>(ptr[i]);
	}
	return val;
}

// Parses a `/Type /XRef` stream object at `stream_offset` using only integer
// `/Length` (or `TerminatorScanner` fallback) because `/Type /XRef` streams are
// parsed while constructing the cross-reference table.
[[nodiscard]] PdfResult<IndirectObject> parse_xref_stream_object_at(
	std::span<const uint8_t> file_bytes,
	uint64_t stream_offset)
{
	if (stream_offset >= file_bytes.size()) {
		return make_xref_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedXRef,
			"Cross-reference stream byte offset exceeds file bounds",
			stream_offset);
	}
	Parser parser(file_bytes);
	parser.seek(static_cast<size_t>(stream_offset));
	auto obj_res = parser.parse_indirect_object();
	if (!obj_res.has_value()) {
		return obj_res.error();
	}
	if (!obj_res->value.is_stream()) {
		return make_xref_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedXRef,
			"Expected indirect stream object at cross-reference stream offset",
			stream_offset,
			obj_res->ref.obj_num,
			obj_res->ref.gen_num);
	}
	return obj_res;
}


// Whether a recovered `/Type /ObjStm` container needs other objects to be decoded or read: its /Filter, /DecodeParms,
// /N or /First is an indirect reference or holds one (an array element, or a value inside a parameter dictionary).
[[nodiscard]] bool objstm_needs_resolver(const PdfDict& dict) noexcept
{
	const auto dict_holds_ref = [](const PdfDict& d) {
		return std::any_of(d.entries().begin(), d.entries().end(), [](const auto& e) { return e.second.is_ref(); });
	};
	const auto holds_ref = [&](const PdfValue* v) {
		if (v == nullptr) {
			return false;
		}
		if (v->is_ref()) {
			return true;
		}
		if (const PdfDict* d = v->as_dict()) {
			return dict_holds_ref(*d);
		}
		if (const PdfArray* a = v->as_array()) {
			return std::any_of(a->begin(), a->end(), [&](const PdfValue& e) {
				return e.is_ref() || (e.as_dict() != nullptr && dict_holds_ref(*e.as_dict()));
			});
		}
		return false;
	};
	return holds_ref(dict.get("Filter")) || holds_ref(dict.get("DecodeParms")) || holds_ref(dict.get("DP")) ||
		holds_ref(dict.get("N")) || holds_ref(dict.get("First"));
}

// The two refusals unpack_xref_stream gives when the cumulative row budget runs out; recovery passes them on as a
// budget refusal instead of dropping the stream.
constexpr std::string_view kXrefRowBudgetExceeded = "Cumulative /Type /XRef entry count exceeds maximum budget";
constexpr std::string_view kXrefDefaultIndexBudgetExceeded =
	"/Type /XRef default /Index [0 /Size] exceeds maximum entry budget";

// What damaged-file recovery's resolver keeps of a parameter target (D06 T05 §29): numbers, booleans, null,
// references and names up to 127 bytes as they are; of a dictionary, only the keys a filter recovery decodes can
// read (the Flate and LZW predictor keys and /EarlyChange); of a short array, its elements reduced the same way; any
// other value becomes a placeholder of no use to a reader (the empty string, or the empty name for an over-long
// name). A filter parameter, /N or /First never needs more, so a large target, however wide, shared by many
// containers is copied in a few bytes.
[[nodiscard]] PdfValue recovery_parameter_value(const PdfValue& value, int depth = 0)
{
	if (const auto b = value.as_bool()) {
		return PdfValue(*b);
	}
	if (const auto i = value.as_int()) {
		return PdfValue(*i);
	}
	if (const auto n = value.as_number()) {
		return PdfValue(*n);
	}
	if (const auto r = value.as_ref()) {
		return PdfValue(*r);
	}
	if (value.is_null()) {
		return PdfValue(PdfNull{});
	}
	if (const auto name = value.as_name()) {
		return name->size() <= 127u ? PdfValue(PdfName::from_decoded(*name)) : PdfValue(PdfName::from_decoded(""));
	}
	if (depth == 0) {
		if (const PdfDict* dict = value.as_dict()) {
			static constexpr std::array<std::string_view, 5> kRecoveryFilterKeys = {
				"Predictor", "Colors", "BitsPerComponent", "Columns", "EarlyChange"};
			PdfDict reduced;
			for (const std::string_view key : kRecoveryFilterKeys) {
				if (const PdfValue* entry = dict->get(key)) {
					reduced.set(PdfName::from_decoded(key), recovery_parameter_value(*entry, depth + 1));
				}
			}
			return PdfValue(std::move(reduced));
		}
		if (const PdfArray* arr = value.as_array(); arr != nullptr && arr->size() <= kMaxFilterChainDepth) {
			PdfArray reduced;
			for (const PdfValue& e : *arr) {
				reduced.push_back(e.is_dict() ? recovery_parameter_value(e, 0) : recovery_parameter_value(e, depth + 1));
			}
			return PdfValue(std::move(reduced));
		}
	}
	return PdfValue(PdfString());
}

} // namespace

PdfResult<uint64_t> XrefTable::locate_header_offset(
	std::span<const uint8_t> file_bytes,
	std::string* out_version) noexcept
{
	try {
		if (file_bytes.empty()) {
			return make_xref_error(
				ErrorCode::NotAPdf,
				DiagnosticId::NotAPdfHeader,
				"Empty input buffer: missing %PDF- header",
				0);
		}

		const size_t search_window = std::min<size_t>(file_bytes.size(), kHeaderSearchBytes);
		std::optional<size_t> first_prefix_pos = std::nullopt;
		if (search_window >= kPdfHeaderPrefix.size()) {
			const size_t max_pos = search_window - kPdfHeaderPrefix.size();
			for (size_t i = 0; i <= max_pos; ++i) {
				if (std::memcmp(
						file_bytes.data() + i,
						kPdfHeaderPrefix.data(),
						kPdfHeaderPrefix.size()) != 0) {
					continue;
				}
				if (!first_prefix_pos.has_value()) {
					first_prefix_pos = i;
				}
				if (file_bytes.size() - i < 8) {
					continue;
				}

				const uint8_t major = file_bytes[i + 5];
				const uint8_t dot = file_bytes[i + 6];
				const uint8_t minor = file_bytes[i + 7];
				const bool valid_version = (dot == '.') &&
					((major == '1' && minor >= '0' && minor <= '9') ||
					 (major == '2' && minor == '0'));
				if (!valid_version) {
					continue;
				}
				if (i + 8 < file_bytes.size() && Lexer::is_pdf_regular(file_bytes[i + 8])) {
					continue;
				}

				if (out_version != nullptr) {
					out_version->assign(
						{static_cast<char>(major), '.', static_cast<char>(minor)});
				}
				return static_cast<uint64_t>(i);
			}
		}

		if (first_prefix_pos.has_value()) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedHeader,
				"Truncated or invalid %PDF-1.x / %PDF-2.0 version header",
				*first_prefix_pos);
		}

		return make_xref_error(
			ErrorCode::NotAPdf,
			DiagnosticId::NotAPdfHeader,
			"Missing %PDF- header within the first 1024 bytes",
			0);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while locating PDF header");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while locating PDF header");
	}
}

PdfResult<uint64_t> XrefTable::locate_startxref_offset(
	std::span<const uint8_t> file_bytes) noexcept
{
	try {
		if (file_bytes.size() < kStartxrefKeyword.size()) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedTrailer,
				"File too small to contain startxref",
				0);
		}

		const size_t window_start = file_bytes.size() > kTailSearchBytes
			? (file_bytes.size() - kTailSearchBytes)
			: 0;
		const size_t last_candidate = file_bytes.size() - kStartxrefKeyword.size();

		std::optional<size_t> found_pos = std::nullopt;
		for (size_t pos = last_candidate + 1; pos > window_start;) {
			--pos;
			if (file_bytes[pos] != 's') {
				continue;
			}
			if (!matches_token_at(file_bytes, pos, kStartxrefKeyword)) {
				continue;
			}
			if (is_on_comment_line(file_bytes, pos)) {
				continue;
			}
			found_pos = pos;
			break;
		}

		if (!found_pos.has_value()) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedTrailer,
				"Missing startxref keyword in trailing 1024 bytes",
				window_start);
		}

		const size_t sx_pos = *found_pos;
		Lexer lexer(file_bytes);
		lexer.seek(sx_pos + kStartxrefKeyword.size());

		Token offset_tok;
		PdfError tok_err;
		if (!next_non_comment_token(lexer, offset_tok, tok_err)) {
			return tok_err;
		}
		if (offset_tok.kind != TokenKind::Integer || offset_tok.int_value < 0) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedTrailer,
				"Invalid or negative byte offset after startxref",
				offset_tok.byte_offset);
		}

		const std::span<const uint8_t> after_offset = file_bytes.subspan(lexer.position());
		if (!contains_eof_marker(after_offset)) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedTrailer,
				"Missing %%EOF marker after startxref offset",
				sx_pos);
		}

		return static_cast<uint64_t>(offset_tok.int_value);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while locating startxref");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while locating startxref");
	}
}

PdfResult<void> XrefTable::unpack_xref_stream(
	const PdfDict& xref_dict,
	std::span<const uint8_t> decoded_bytes,
	uint64_t file_size,
	uint64_t header_offset,
	XrefTable& table,
	uint64_t& total_rows_parsed,
	uint64_t max_total_entries) noexcept
{
	try {
		const uint64_t effective_max_entries =
			std::min<uint64_t>(max_total_entries, kMaxXrefTotalEntries);

		const auto type_name = xref_dict.get_name("Type");
		if (!type_name.has_value() || *type_name != "XRef") {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedXRef,
				"Cross-reference stream dictionary is missing /Type /XRef");
		}

		const auto size_opt = xref_dict.get_int("Size");
		if (!size_opt.has_value() ||
			*size_opt <= 0 ||
			static_cast<uint64_t>(*size_opt) >
				static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1ULL) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedXRef,
				"Cross-reference stream dictionary has missing or invalid /Size");
		}
		const uint64_t dict_size = static_cast<uint64_t>(*size_opt);
		// ISO 32000-1 Table 15 (and Table 17 for streams): an entry numbered above /Size is missing, so it is skipped
		// before its fields are validated. The newest revision's /Size governs: it is already in the merged trailer
		// when an older stream is read, and this stream's own when it is the newest.
		const auto merged_size = table.trailer_.get_int("Size");
		const uint64_t governing_size =
			merged_size.has_value() && *merged_size > 0 ? static_cast<uint64_t>(*merged_size) : dict_size;

		const PdfArray* w_arr = xref_dict.get_array("W");
		if (w_arr == nullptr || w_arr->size() != 3u) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedXRef,
				"Cross-reference stream dictionary requires 3-element /W array");
		}

		std::array<size_t, 3> w{};
		for (size_t i = 0; i < 3; ++i) {
			const auto wi = (*w_arr)[i].as_int();
			if (!wi.has_value() || *wi < 0 || *wi > 8) {
				return make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedXRef,
					"Cross-reference stream /W field width must be in 0..8 bytes");
			}
			w[i] = static_cast<size_t>(*wi);
		}
		const size_t entry_stride = w[0] + w[1] + w[2];
		if (entry_stride == 0) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedXRef,
				"Cross-reference stream /W field widths cannot sum to zero");
		}

		struct SubsectionSpan {
			uint64_t first_id{0};
			uint64_t count{0};
		};
		std::vector<SubsectionSpan> subsections;
		uint64_t stream_row_count = 0;

		if (const PdfValue* index_val = xref_dict.get("Index")) {
			const PdfArray* index_arr = index_val->as_array();
			if (index_arr == nullptr || index_arr->empty() || (index_arr->size() % 2u) != 0u) {
				return make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedXRef,
					"Malformed /Index array in /Type /XRef stream (expected even number of integers)");
			}
			subsections.reserve(index_arr->size() / 2u);
			for (size_t i = 0; i < index_arr->size(); i += 2u) {
				const auto first_opt = (*index_arr)[i].as_int();
				const auto count_opt = (*index_arr)[i + 1].as_int();
				if (!first_opt.has_value() || !count_opt.has_value() ||
					*first_opt < 0 || *count_opt < 0 ||
					*first_opt > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) ||
					*count_opt > static_cast<int64_t>(kMaxXrefSubsectionCount)) {
					return make_xref_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedXRef,
						"Invalid or negative subsection pair in /Type /XRef /Index array");
				}
				const uint64_t first_id = static_cast<uint64_t>(*first_opt);
				const uint64_t count = static_cast<uint64_t>(*count_opt);
				if (first_id + count >
					static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1ULL) {
					return make_xref_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedXRef,
						"/Index subsection object range overflows 32-bit object numbers");
				}
				if (count > effective_max_entries - total_rows_parsed ||
					count > effective_max_entries - stream_row_count) {
					return make_xref_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedXRef,
						kXrefRowBudgetExceeded);
				}
				total_rows_parsed += count;
				stream_row_count += count;
				subsections.push_back(SubsectionSpan{first_id, count});
			}
		} else {
			if (dict_size > kMaxXrefSubsectionCount ||
				dict_size > effective_max_entries - total_rows_parsed) {
				return make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedXRef,
					kXrefDefaultIndexBudgetExceeded);
			}
			total_rows_parsed += dict_size;
			stream_row_count = dict_size;
			subsections.push_back(SubsectionSpan{0u, dict_size});
		}

		const uint64_t required_bytes = stream_row_count * static_cast<uint64_t>(entry_stride);
		if (decoded_bytes.size() < required_bytes) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedXRef,
				"Decoded /Type /XRef stream payload is shorter than sum(count_i) * (w1 + w2 + w3)");
		}

		size_t cursor = 0;
		for (const SubsectionSpan& sub : subsections) {
			for (uint64_t i = 0; i < sub.count; ++i) {
				const uint8_t* rec = decoded_bytes.data() + cursor;
				cursor += entry_stride;

				const uint64_t field1 = (w[0] == 0u) ? 1ULL : read_big_endian_field(rec, w[0]);
				const uint64_t field2 = read_big_endian_field(rec + w[0], w[1]);
				const uint64_t field3 = read_big_endian_field(rec + w[0] + w[1], w[2]);
				const uint32_t obj_num = static_cast<uint32_t>(sub.first_id + i);
				if (static_cast<uint64_t>(obj_num) > governing_size) {
					record_diagnostic(ErrorCode::DamagedFormat, DiagnosticId::DamagedXRef, std::nullopt, obj_num, 0);
					continue;
				}

				if (field1 == 0ULL) {
					if (field3 > std::numeric_limits<uint16_t>::max()) {
						return make_xref_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedXRef,
							"Type-0 XRef stream entry generation overflows 16 bits",
							std::nullopt,
							obj_num);
					}
					const uint16_t gen = static_cast<uint16_t>(field3);
					table.entries_.try_emplace(
						obj_num,
						XrefEntry{XrefEntryKind::Free, field2, 0u, 0u, gen});
				} else if (field1 == 1ULL) {
					if (field3 > std::numeric_limits<uint16_t>::max()) {
						return make_xref_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedXRef,
							"Type-1 XRef stream entry generation overflows 16 bits",
							std::nullopt,
							obj_num);
					}
					const uint16_t gen = static_cast<uint16_t>(field3);
					if (obj_num == 0u) {
						table.entries_.try_emplace(
							0u,
							XrefEntry{XrefEntryKind::Free, field2, 0u, 0u, gen});
						continue;
					}
					if (field2 > file_size - header_offset ||
						field2 + header_offset >= file_size) {
						return make_xref_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedXRef,
							"Type-1 XRef stream entry offset points outside file bounds",
							field2,
							obj_num,
							gen);
					}
					table.entries_.try_emplace(
						obj_num,
						XrefEntry{
							XrefEntryKind::Uncompressed,
							field2 + header_offset,
							0u,
							0u,
							gen});
				} else if (field1 == 2ULL) {
					if (obj_num == 0u ||
						field2 == 0ULL ||
						field2 > std::numeric_limits<uint32_t>::max() ||
						field3 > std::numeric_limits<uint32_t>::max()) {
						return make_xref_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedXRef,
							"Invalid type-2 compressed XRef stream entry fields",
							std::nullopt,
							obj_num);
					}
					table.entries_.try_emplace(
						obj_num,
						XrefEntry{
							XrefEntryKind::Compressed,
							0u,
							static_cast<uint32_t>(field2),
							static_cast<uint32_t>(field3),
							0u});
				} else {
					// Per ISO 32000-1 section 7.5.8.3, unknown entry types in an XRef
					// stream are treated as null (free) references so newer PDF
					// extensions do not break readers.
					table.entries_.try_emplace(
						obj_num,
						XrefEntry{XrefEntryKind::Free, 0u, 0u, 0u, 0u});
				}
			}
		}

		return merge_trailer_from_revision(table.trailer_, xref_dict);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while unpacking /Type /XRef stream");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while unpacking /Type /XRef stream");
	}
}

PdfResult<XrefTable> XrefTable::recover_damaged_xref(
	std::span<const uint8_t> file_bytes,
	uint64_t max_total_entries) noexcept
{
	try {
		const uint64_t effective_max_entries =
			std::min<uint64_t>(max_total_entries, kMaxXrefTotalEntries);
		std::string version_str;
		RECTO_PDF_TRY(const uint64_t header_off, locate_header_offset(file_bytes, &version_str));

		record_diagnostic(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedXRef,
			header_off);

		XrefTable table;
		table.header_offset_ = header_off;
		table.pdf_version_ = std::move(version_str);
		table.revision_count_ = 1u;
		table.recovered_ = true;
		table.entries_.emplace(
			0u,
			XrefEntry{XrefEntryKind::Free, 0u, 0u, 0u, 65535u});

		uint64_t objects_discovered = 0;
		uint64_t candidate_attempts = 0;
		uint64_t cumulative_parse_bytes = 0;
		uint64_t cumulative_decoded_objstm_bytes = 0;
		const uint64_t max_recovery_parse_bytes = std::max<uint64_t>(
			static_cast<uint64_t>(file_bytes.size()) * 4ULL,
			65536ULL);
		const uint64_t max_recovery_decoded_objstm_bytes = std::min<uint64_t>(
			kMaxDecodedStreamBytes,
			std::max<uint64_t>(effective_max_entries, 1ULL) * 65536ULL);
		uint32_t max_obj_num = 0;
		std::optional<IndirectRef> info_candidate;
		// The file position of info_candidate's definition, so a member unpacked after the scan replaces it only when
		// it comes later in the file.
		uint64_t info_candidate_pos = 0;
		std::unordered_map<uint32_t, std::pair<uint16_t, int64_t>> recovered_integers;
		std::unordered_map<uint32_t, uint16_t> catalog_generations;
		// Dictionaries whose /Type is an indirect reference: the named object may lie after them in the file, so
		// they are checked once the scan has found every object (generation, /Type reference).
		std::unordered_map<uint32_t, std::pair<uint16_t, IndirectRef>> indirect_type_dicts;
		// Objects the scan found to be the name /Catalog (generation), so an indirect /Type is checked with no further
		// parsing, as recovered_integers serves an indirect /Length.
		std::unordered_map<uint32_t, uint16_t> catalog_names;
		// Catalog candidates with the file position of their definition (a compressed one has its container's), so
		// the fallback below can prefer the latest in the file even for members unpacked after the scan.
		std::vector<std::pair<uint64_t, IndirectRef>> catalog_history;
		std::unordered_map<uint32_t, std::vector<uint32_t>> objstm_members;
		// The file position of the definition each entry came from (an uncompressed object's own, or its container's),
		// so a container unpacked after the scan overrides exactly the definitions before it in the file and none after.
		std::unordered_map<uint32_t, uint64_t> defined_at;
		// Containers whose /Filter, /DecodeParms, /N or /First is indirect: the object a reference names may lie after
		// the container, so they are unpacked once the scan has found every object. Every other container is still
		// unpacked where the scan meets it, so its integers keep serving a later indirect /Length.
		// Only where each one is: an encrypted file's containers are never decoded during recovery, so their bytes are
		// not kept; a container that is decoded is parsed again at its position after the scan.
		struct DeferredObjStm {
			uint32_t obj_num = 0;
			size_t pos = 0;
		};
		// Rows read from every recovered cross-reference stream, against one budget for the whole recovery.
		uint64_t recovered_xref_rows = 0;
		// Every container is deferred: its position is kept and it is unpacked after the scan, once the trailers the
		// scan parsed say whether the file is encrypted (an encrypted file's containers are ciphertext until
		// authentication, so none is unpacked and its compressed objects come from the intact xref streams' rows). No
		// guess about encryption is made before that, so nothing in the file's bytes can change which containers are
		// read.
		std::vector<DeferredObjStm> deferred_objstms;

		// What the scan learned about one definition of an object (ISO 32000-1 7.5.8: a later xref row may name an older
		// definition again), kept when a later definition replaces it so that row restores what was learned.
		struct ObjectFacts {
			std::optional<uint16_t> catalog_name;
			std::optional<std::pair<uint16_t, int64_t>> integer;
			std::optional<std::pair<uint16_t, IndirectRef>> indirect_type;
			std::optional<uint16_t> catalog_generation;
			std::optional<IndirectRef> info;

			[[nodiscard]] bool empty() const noexcept
			{
				return !catalog_name && !integer && !indirect_type && !catalog_generation && !info;
			}
		};
		const auto facts_of = [&](uint32_t id) {
			ObjectFacts facts;
			if (const auto it = catalog_names.find(id); it != catalog_names.end()) {
				facts.catalog_name = it->second;
			}
			if (const auto it = recovered_integers.find(id); it != recovered_integers.end()) {
				facts.integer = it->second;
			}
			if (const auto it = indirect_type_dicts.find(id); it != indirect_type_dicts.end()) {
				facts.indirect_type = it->second;
			}
			if (const auto it = catalog_generations.find(id); it != catalog_generations.end()) {
				facts.catalog_generation = it->second;
			}
			if (info_candidate.has_value() && info_candidate->obj_num == id) {
				facts.info = *info_candidate;
			}
			return facts;
		};
		const auto restore_facts = [&](uint32_t id, const ObjectFacts& facts, uint64_t at) {
			if (facts.catalog_name) {
				catalog_names[id] = *facts.catalog_name;
			}
			if (facts.integer) {
				recovered_integers[id] = *facts.integer;
			}
			if (facts.indirect_type) {
				indirect_type_dicts[id] = *facts.indirect_type;
			}
			if (facts.catalog_generation) {
				catalog_generations[id] = *facts.catalog_generation;
			}
			if (facts.info && (!info_candidate.has_value() || at >= info_candidate_pos)) {
				info_candidate = *facts.info;
				info_candidate_pos = at;
			}
		};
		// A replaced container member's facts, by (container position, index), with the member's object number; kept only
		// when there are facts, so bounded by the members the scan decoded.
		std::map<std::pair<uint64_t, uint32_t>, std::pair<uint32_t, ObjectFacts>> replaced_members;
		// Every uncompressed definition the scan found, in file order (so sorted by offset), and the facts of one a later
		// definition replaced, by offset: a later type-1 xref row naming an older definition restores it. Both are capped
		// at the recovery's entry budget; a definition past the cap is not restorable by a row (the scan's own choice,
		// the later definition, stands).
		struct ScannedDefinition {
			uint64_t offset = 0;
			uint32_t obj_num = 0;
			uint16_t generation = 0;
		};
		std::vector<ScannedDefinition> scanned_definitions;
		std::unordered_map<uint64_t, ObjectFacts> definition_facts;
		const auto find_definition = [&](uint64_t offset) -> const ScannedDefinition* {
			const auto it = std::lower_bound(scanned_definitions.begin(), scanned_definitions.end(), offset,
				[](const ScannedDefinition& d, uint64_t value) { return d.offset < value; });
			return it != scanned_definitions.end() && it->offset == offset ? &*it : nullptr;
		};
		const auto remember_replaced = [&](uint32_t id) {
			const XrefEntry* before = table.find(id);
			if (before == nullptr) {
				return;
			}
			ObjectFacts facts = facts_of(id);
			if (facts.empty()) {
				return;
			}
			if (before->is_uncompressed()) {
				if (definition_facts.size() < effective_max_entries) {
					definition_facts[before->byte_offset] = std::move(facts);
				}
			} else if (before->is_compressed()) {
				const XrefEntry* container = table.find(before->obj_stm_num);
				if (container != nullptr && container->is_uncompressed()) {
					replaced_members[{container->byte_offset, before->obj_stm_index}] = {id, std::move(facts)};
				}
			}
		};
		auto invalidate_objstm_container = [&](uint32_t container_id) {
			auto m_it = objstm_members.find(container_id);
			if (m_it == objstm_members.end()) {
				return;
			}
			for (const uint32_t member_id : m_it->second) {
				const XrefEntry* me = table.find(member_id);
				if (me != nullptr && me->is_compressed() && me->obj_stm_num == container_id) {
					table.entries_.erase(member_id);
					defined_at.erase(member_id);
					if (objects_discovered > 0u) {
						--objects_discovered;
					}
					recovered_integers.erase(member_id);
					catalog_generations.erase(member_id);
					indirect_type_dicts.erase(member_id);
					catalog_names.erase(member_id);
					if (info_candidate.has_value() && info_candidate->obj_num == member_id) {
						info_candidate = std::nullopt;
					}
				}
			}
			objstm_members.erase(m_it);
		};

		// Reads a decoded container's header and records its members, as the scan does for one it meets. Members are
		// read by the same routine as on every other path (extract_object_stream_member).
		// Reads a decoded container's header (ISO 32000-1 7.5.7): its (object, offset) pairs, or nothing when the header
		// is not one (a container recovery then leaves alone).
		const auto read_objstm_header = [&](uint32_t obj_num, std::optional<int64_t> n_opt, std::optional<int64_t> first_opt,
			std::span<const uint8_t> decoded) -> PdfResult<std::optional<CachedObjStream>> {
			if (!(n_opt.has_value() && *n_opt > 0 && *n_opt <= static_cast<int64_t>(kMaxObjStmEntries) &&
					first_opt.has_value() && *first_opt >= 0 && static_cast<size_t>(*first_opt) <= decoded.size())) {
				return std::optional<CachedObjStream>{};
			}
			const size_t first_off = static_cast<size_t>(*first_opt);
			Lexer hdr_lex(decoded.first(first_off));
			CachedObjStream header;
			header.first_offset = first_off;
			header.entries.reserve(static_cast<size_t>(*n_opt));
			for (int64_t idx = 0; idx < *n_opt; ++idx) {
				Token id_tok;
				Token off_tok;
				PdfError herr;
				if (!next_non_comment_token(hdr_lex, id_tok, herr) || !next_non_comment_token(hdr_lex, off_tok, herr)) {
					if (herr.code == ErrorCode::OutOfMemory) {
						return herr;
					}
					return std::optional<CachedObjStream>{};
				}
				if (id_tok.kind != TokenKind::Integer || id_tok.int_value <= 0 ||
					id_tok.int_value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) ||
					id_tok.int_value == static_cast<int64_t>(obj_num) || off_tok.kind != TokenKind::Integer ||
					off_tok.int_value < 0) {
					return std::optional<CachedObjStream>{};
				}
				const uint64_t rel_u64 = static_cast<uint64_t>(off_tok.int_value);
				if (rel_u64 > decoded.size() - first_off ||
					(!header.entries.empty() && static_cast<size_t>(rel_u64) < header.entries.back().rel_offset)) {
					return std::optional<CachedObjStream>{};
				}
				header.entries.push_back(
					CachedObjStream::HeaderPair{static_cast<uint32_t>(id_tok.int_value), static_cast<size_t>(rel_u64)});
			}
			return std::optional<CachedObjStream>(std::move(header));
		};

		auto unpack_objstm = [&](uint32_t obj_num, size_t container_pos, std::optional<int64_t> n_opt,
			std::optional<int64_t> first_opt, std::vector<uint8_t> decoded) -> PdfResult<void> {
			RECTO_PDF_TRY_REF(header_opt, read_objstm_header(obj_num, n_opt, first_opt, decoded));
			const bool header_ok = header_opt.has_value();
			{
				if (header_ok) {
					// Members are read by the same routine as on every other path
					// (extract_object_stream_member); recovery skips an invalid one instead
					// of failing, and keeps the rest of the container.
					CachedObjStream recovered_stm = std::move(*header_opt);
					recovered_stm.decoded_bytes = std::move(decoded);
					struct InnerPair {
						uint32_t obj_num = 0;
					};
					std::vector<InnerPair> inner_pairs;
					inner_pairs.reserve(recovered_stm.entries.size());
					for (const CachedObjStream::HeaderPair& pair : recovered_stm.entries) {
						inner_pairs.push_back(InnerPair{pair.obj_num});
					}
					auto& member_vec = objstm_members[obj_num];
					for (size_t idx = 0; idx < inner_pairs.size(); ++idx) {
						if (candidate_attempts >= effective_max_entries) {
							return make_xref_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedXRef,
								"Damaged-file recovery exceeded maximum object entry budget",
								container_pos);
						}
						++candidate_attempts;
						const uint32_t inner_id = inner_pairs[idx].obj_num;
						// A definition later in the file than this container wins (only a container unpacked after the scan
						// can meet one).
						if (const auto later = defined_at.find(inner_id);
							later != defined_at.end() && later->second > container_pos) {
							// A later xref row naming exactly this member (same container and index) is the same object:
							// its content is still read below (a catalog, an integer). Any other later definition wins.
							const XrefEntry* existing = table.find(inner_id);
							const bool same_member = existing != nullptr && existing->is_compressed() &&
								existing->obj_stm_num == obj_num && existing->obj_stm_index == static_cast<uint32_t>(idx);
							if (!same_member) {
								continue;
							}
						}

						auto member = extract_object_stream_member(
							recovered_stm,
							static_cast<uint32_t>(idx),
							IndirectRef{inner_id, 0u});
						if (!member.has_value()) {
							if (member.error().code == ErrorCode::OutOfMemory) {
								return member.error();
							}
							continue;
						}
						const PdfValue* inner_val = &*member;

						if (!table.entries_.contains(inner_id) &&
							objects_discovered >= effective_max_entries) {
							return make_xref_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedXRef,
								"Damaged-file recovery exceeded maximum object entry budget",
								container_pos);
						}
						remember_replaced(inner_id);
						invalidate_objstm_container(inner_id);
						if (!table.entries_.contains(inner_id)) {
							++objects_discovered;
						}
						table.set_entry(
							inner_id,
							XrefEntry{
								XrefEntryKind::Compressed,
								0u,
								obj_num,
								static_cast<uint32_t>(idx),
								0u});
						member_vec.push_back(inner_id);
						// Never earlier than a later xref row that names this same member (its position keeps its authority).
						defined_at[inner_id] = std::max(defined_at[inner_id], static_cast<uint64_t>(container_pos));
						recovered_integers.erase(inner_id);
						catalog_generations.erase(inner_id);
						indirect_type_dicts.erase(inner_id);
						catalog_names.erase(inner_id);
						if (info_candidate.has_value() &&
							info_candidate->obj_num == inner_id) {
							info_candidate = std::nullopt;
						}
						if (inner_id > max_obj_num) {
							max_obj_num = inner_id;
						}
						if (inner_val->as_name() == std::optional<std::string_view>("Catalog")) {
							catalog_names[inner_id] = 0u;
						}
						if (const auto int_v = inner_val->as_int()) {
							recovered_integers[inner_id] = {uint16_t{0}, *int_v};
						} else if (const PdfDict* idict = inner_val->as_dict()) {
							const auto itype = idict->get_name("Type");
							if (const PdfValue* itype_val = idict->get("Type");
								itype_val != nullptr && itype_val->is_ref()) {
								indirect_type_dicts[inner_id] = {uint16_t{0}, *itype_val->as_ref()};
								catalog_history.push_back({container_pos, IndirectRef{inner_id, 0u}});
							}
							if (itype.has_value() && *itype == "Catalog") {
								catalog_generations[inner_id] = 0u;
								catalog_history.push_back({container_pos, IndirectRef{inner_id, 0u}});
							} else if (
								!itype.has_value() &&
								(idict->contains("Title") ||
								 idict->contains("Author") ||
								 idict->contains("Producer") ||
								 idict->contains("CreationDate"))) {
								if (!info_candidate.has_value() || container_pos >= info_candidate_pos) {
								info_candidate = IndirectRef{inner_id, 0u};
								info_candidate_pos = container_pos;
							}
							}
						}
					}
				}
			}
			return {};
		};

		using TargetCache = std::map<std::tuple<uint32_t, uint16_t, uint64_t>, PdfValue>;
		// A container's parameters resolve one level through the objects the scan found uncompressed (the latest
		// definition of each, as far as the scan has come); a reference to anything else, or to an object that does not
		// parse, is null, as an undefined object is (ISO 32000-1 section 7.3.10), so resolution never recurses. Parsing is
		// charged to the scan's cumulative parse budget.
		bool resolver_over_budget = false;
		const auto parse_target = [&](IndirectRef ref) -> PdfResult<PdfValue> {
			const XrefEntry* entry = table.find(ref.obj_num);
			if (entry == nullptr || !entry->is_uncompressed() || entry->generation != ref.gen_num ||
				entry->byte_offset >= file_bytes.size()) {
				return PdfValue(PdfNull{});
			}
			const size_t at = static_cast<size_t>(entry->byte_offset);
			Parser parser(file_bytes);
			parser.seek(at);
			auto parsed = parser.parse_indirect_object(
				[&recovered_integers, ref](IndirectRef len_ref) -> PdfResult<int64_t> {
					const auto it = recovered_integers.find(len_ref.obj_num);
					if (len_ref.obj_num == ref.obj_num || it == recovered_integers.end() ||
						it->second.first != len_ref.gen_num) {
						return make_pdf_error(
							ErrorCode::DamagedFormat,
							"Indirect /Length target not available as an integer object during recovery");
					}
					return it->second.second;
				});
			cumulative_parse_bytes += static_cast<uint64_t>(
				std::max(parser.lexer().max_scanned_offset(), parser.lexer().position()) - at);
			if (cumulative_parse_bytes > max_recovery_parse_bytes) {
				resolver_over_budget = true;
				return make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedXRef,
					"Damaged-file recovery exceeded cumulative parse byte budget",
					at);
			}
			if (!parsed.has_value()) {
				if (parsed.error().code == ErrorCode::OutOfMemory) {
					return parsed.error();
				}
				return PdfValue(PdfNull{});
			}
			return std::move(parsed->value);
		};
		// A resolver over `parse_target` that parses each target once into `targets`, kept reduced to what a parameter can
		// use, so a parameter object many containers share is parsed and charged once and each lookup copies a few
		// scalars. A target is keyed by the definition it was read from (object, generation and offset), so a later
		// definition is read afresh, and a target with no definition yet is not remembered.
		const auto caching_resolver = [&table, &parse_target](TargetCache& targets) -> IndirectObjectResolver {
			return [&table, &parse_target, &targets](IndirectRef ref) -> PdfResult<PdfValue> {
				const XrefEntry* entry = table.find(ref.obj_num);
				if (entry == nullptr || !entry->is_uncompressed() || entry->generation != ref.gen_num) {
					return PdfValue(PdfNull{});
				}
				const auto target_key = std::tuple{ref.obj_num, ref.gen_num, entry->byte_offset};
				if (const auto known = targets.find(target_key); known != targets.end()) {
					return known->second.deep_clone();
				}
				RECTO_PDF_TRY_REF(parsed_value, parse_target(ref));
				if (parsed_value.is_stream()) {
					return parsed_value; // a container is parsed once anyway; its bytes are not kept
				}
				const auto kept = targets.emplace(target_key, recovery_parameter_value(parsed_value)).first;
				return kept->second.deep_clone();
			};
		};
		const auto over_budget_error = [&](size_t at) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedXRef,
				"Damaged-file recovery exceeded cumulative parse byte budget",
				at);
		};
		// What deciding to decode a container reads, each value resolved one level through `resolve`: its filter chain as
		// names (anything else reads as 0, which no filter is), and /N and /First.
		struct ContainerView {
			PdfDict filters;
			std::optional<int64_t> n;
			std::optional<int64_t> first;
		};
		const auto container_view = [&](const PdfDict& dict, const IndirectObjectResolver& resolve) -> PdfResult<ContainerView> {
			const auto filter_name = [&](const PdfValue& v) -> PdfResult<PdfValue> {
				if (const auto ref = v.as_ref()) {
					RECTO_PDF_TRY_REF(target, resolve(*ref));
					const auto name = target.as_name();
					return name.has_value() ? PdfValue(PdfName::from_decoded(*name)) : PdfValue(int64_t{0});
				}
				const auto name = v.as_name();
				return name.has_value() ? PdfValue(PdfName::from_decoded(*name)) : PdfValue(int64_t{0});
			};
			const auto int_value = [&](const PdfValue* v) -> PdfResult<std::optional<int64_t>> {
				if (v == nullptr) {
					return std::optional<int64_t>{};
				}
				if (const auto ref = v->as_ref()) {
					RECTO_PDF_TRY_REF(target, resolve(*ref));
					return target.as_int();
				}
				return v->as_int();
			};
			ContainerView view;
			if (const PdfValue* filter = dict.get("Filter"); filter != nullptr && !filter->is_null()) {
				const PdfValue* filter_value = filter;
				std::optional<PdfValue> filter_target;
				if (const auto ref = filter->as_ref()) {
					RECTO_PDF_TRY_REF(target, resolve(*ref));
					filter_target.emplace(std::move(target));
					filter_value = &*filter_target;
				}
				if (const PdfArray* arr = filter_value->as_array(); arr != nullptr && arr->size() <= kMaxFilterChainDepth) {
					PdfArray names;
					for (const PdfValue& e : *arr) {
						RECTO_PDF_TRY_REF(name, filter_name(e));
						names.push_back(std::move(name));
					}
					view.filters.set("Filter", PdfValue(std::move(names)));
				} else if (filter_value->is_null()) {
					view.filters.set("Filter", PdfValue(PdfNull{}));
				} else {
					RECTO_PDF_TRY_REF(name, filter_name(*filter_value));
					view.filters.set("Filter", std::move(name));
				}
			}
			RECTO_PDF_TRY_REF(n, int_value(dict.get("N")));
			RECTO_PDF_TRY_REF(first, int_value(dict.get("First")));
			view.n = n;
			view.first = first;
			return view;
		};

		// A stream's indirect /Length can name an integer inside a container the scan has passed but not unpacked. Such
		// integers are read on demand: the containers met so far are decoded once each, in file order, within what the
		// decode budget has left, and their integer members kept with the container's position. A container that cannot
		// be decoded here (indirect parameters, a filter the decode meter does not measure, too little budget left) is
		// skipped, and a probe never fails recovery for the file's sake. What a probe decodes is kept, so unpacking the
		// container after the scan decodes nothing twice; an encrypted file's containers are never unpacked, and an
		// integer read from ciphertext only ever sizes a stream the parser then checks for `endstream`.
		struct ProbedContainer {
			std::optional<int64_t> n;
			std::optional<int64_t> first;
			std::vector<uint8_t> decoded;
		};
		std::map<std::pair<uint32_t, size_t>, ProbedContainer> probed_containers;
		// Integer members read by a probe: by object (the latest container in the file holding it, with that
		// container's position) and by (container position, index), the address a compressed xref entry names.
		std::unordered_map<uint32_t, std::pair<uint64_t, int64_t>> container_integers;
		std::map<std::pair<uint64_t, uint32_t>, std::pair<uint32_t, int64_t>> probed_members;
		size_t containers_probed = 0;
		bool probe_budget_spent = false;
		// Probes have an allowance of their own, the size of the recovery's decode budget, so decoding what may turn out
		// to be ciphertext never spends what the xref streams and the unpack after the scan need.
		uint64_t cumulative_probe_bytes = 0;
		TargetCache probe_targets;
		const IndirectObjectResolver probe_resolver = caching_resolver(probe_targets);
		// Containers a probe met before the scan had defined a parameter they name, each with the count of definitions
		// the scan had made then: tried again once something new has been defined, never dropped for being early.
		std::vector<std::pair<size_t, uint64_t>> probe_waiting;
		uint64_t definitions_made = 0;
		// Whether a parameter the dictionary names by reference (one level: /Filter, /DecodeParms and their elements,
		// /N, /First) has no uncompressed definition yet.
		const auto names_undefined_parameter = [&](const PdfDict& dict) {
			const auto undefined = [&](const PdfValue* v) {
				const auto ref = v != nullptr ? v->as_ref() : std::nullopt;
				if (!ref.has_value()) {
					return false;
				}
				const XrefEntry* entry = table.find(ref->obj_num);
				return entry == nullptr || !entry->is_uncompressed() || entry->generation != ref->gen_num;
			};
			for (const std::string_view key : {"Filter", "DecodeParms", "N", "First"}) {
				const PdfValue* value = dict.get(PdfName::from_decoded(key));
				if (undefined(value)) {
					return true;
				}
				if (const PdfArray* arr = value != nullptr ? value->as_array() : nullptr; arr != nullptr) {
					for (const PdfValue& e : *arr) {
						if (undefined(&e)) {
							return true;
						}
					}
				}
			}
			return false;
		};
		const auto integers_only = [&recovered_integers](IndirectRef len_ref) -> PdfResult<int64_t> {
			const auto it = recovered_integers.find(len_ref.obj_num);
			if (it == recovered_integers.end() || it->second.first != len_ref.gen_num) {
				return make_pdf_error(
					ErrorCode::DamagedFormat,
					"Indirect /Length target not available as an integer object during recovery");
			}
			return it->second.second;
		};
		enum class ProbeOutcome : uint8_t { Done, Waiting };
		const auto probe_one = [&](const DeferredObjStm container_at) -> PdfResult<ProbeOutcome> {
			{
				Parser parser(file_bytes);
				parser.seek(container_at.pos);
				auto parsed = parser.parse_indirect_object(integers_only);
				cumulative_parse_bytes += static_cast<uint64_t>(
					std::max(parser.lexer().max_scanned_offset(), parser.lexer().position()) - container_at.pos);
				if (!parsed.has_value()) {
					if (parsed.error().code == ErrorCode::OutOfMemory) {
						return parsed.error();
					}
					return ProbeOutcome::Done;
				}
				const PdfStream* container = parsed->value.as_stream();
				if (container == nullptr) {
					return ProbeOutcome::Done;
				}
				if (names_undefined_parameter(container->dict)) {
					return ProbeOutcome::Waiting; // a parameter the scan has not reached yet
				}
				// Indirect parameters resolve through what the scan has found so far, so such a container is unpacked
				// again after the scan with what the whole file says; a container with direct parameters reuses the
				// probe's bytes.
				const bool indirect_parameters = objstm_needs_resolver(container->dict);
				auto view = container_view(container->dict, probe_resolver);
				if (!view.has_value()) {
					if (resolver_over_budget) {
						return over_budget_error(container_at.pos);
					}
					return ProbeOutcome::Done;
				}
				if (!filters_are_metered_lossless(view->filters)) {
					return ProbeOutcome::Done;
				}
				const uint64_t left = max_recovery_decoded_objstm_bytes - cumulative_probe_bytes;
				detail::ScopedDecodeWorkMeter decode_work;
				auto decoded = decode_stream(*container, static_cast<size_t>(std::min<uint64_t>(kMaxDecodedStreamBytes, left)),
					&probe_resolver);
				if (resolver_over_budget) {
					return over_budget_error(container_at.pos);
				}
				if (!decoded.has_value() && decoded.error().code == ErrorCode::OutOfMemory) {
					return decoded.error();
				}
				cumulative_probe_bytes += std::min<uint64_t>(left,
					std::max<uint64_t>(decode_work.bytes(), decoded.has_value() ? static_cast<uint64_t>(decoded->size()) : 0u));
				if (!decoded.has_value()) {
					// Out of budget, the probe stops; any other failure leaves this container to the unpack after the scan.
					probe_budget_spent = decoded.error().code == ErrorCode::DecompressionLimitExceeded;
					return ProbeOutcome::Done;
				}
				const std::optional<int64_t> n_opt = view->n;
				const std::optional<int64_t> first_opt = view->first;
				std::vector<uint8_t> bytes = std::move(*decoded);
				RECTO_PDF_TRY_REF(header_opt, read_objstm_header(container_at.obj_num, n_opt, first_opt, bytes));
				if (header_opt.has_value()) {
					CachedObjStream members = std::move(*header_opt);
					members.decoded_bytes = std::move(bytes);
					for (size_t idx = 0; idx < members.entries.size(); ++idx) {
						auto member = extract_object_stream_member(
							members, static_cast<uint32_t>(idx), IndirectRef{members.entries[idx].obj_num, 0u});
						if (!member.has_value()) {
							if (member.error().code == ErrorCode::OutOfMemory) {
								return member.error();
							}
							continue;
						}
						if (const auto value = member->as_int()) {
							// The latest container in the file holds an object, whichever order the probe read them in.
							auto [latest, fresh] = container_integers.try_emplace(
								members.entries[idx].obj_num, static_cast<uint64_t>(container_at.pos), *value);
							if (!fresh && latest->second.first <= static_cast<uint64_t>(container_at.pos)) {
								latest->second = {static_cast<uint64_t>(container_at.pos), *value};
							}
							probed_members[{static_cast<uint64_t>(container_at.pos), static_cast<uint32_t>(idx)}] =
								{members.entries[idx].obj_num, *value};
						}
					}
					bytes = std::move(members.decoded_bytes);
				}
				if (!indirect_parameters) {
					probed_containers[{container_at.obj_num, container_at.pos}] = ProbedContainer{n_opt, first_opt, std::move(bytes)};
				}
			}
			return ProbeOutcome::Done;
		};
		const auto probe_containers = [&]() -> PdfResult<void> {
			// First the containers that waited, once the scan has defined something since they last tried.
			for (size_t i = 0; i < probe_waiting.size() && !probe_budget_spent;) {
				auto& [index, seen] = probe_waiting[i];
				if (seen == definitions_made) {
					++i;
					continue;
				}
				RECTO_PDF_TRY_REF(outcome, probe_one(deferred_objstms[index]));
				if (outcome == ProbeOutcome::Waiting) {
					seen = definitions_made;
					++i;
				} else {
					probe_waiting.erase(probe_waiting.begin() + static_cast<std::ptrdiff_t>(i));
				}
			}
			for (; containers_probed < deferred_objstms.size() && !probe_budget_spent; ++containers_probed) {
				RECTO_PDF_TRY_REF(outcome, probe_one(deferred_objstms[containers_probed]));
				if (outcome == ProbeOutcome::Waiting) {
					probe_waiting.emplace_back(containers_probed, definitions_made);
				}
			}
			return {};
		};
		// The scan's /Length lookup: an uncompressed integer, or a container member the scan has passed that is later in
		// the file than it.
		const auto scan_length = [&](IndirectRef len_ref, uint32_t stream_obj) -> PdfResult<int64_t> {
			if (len_ref.obj_num == stream_obj) {
				return make_pdf_error(
					ErrorCode::DamagedFormat,
					"Indirect /Length target cannot reference its own stream object");
			}
			const auto unavailable = [] {
				return make_pdf_error(
					ErrorCode::DamagedFormat,
					"Indirect /Length target not available as an integer object during recovery");
			};
			const XrefEntry* entry = table.find(len_ref.obj_num);
			// An intact xref row selected a member: that member, and no other container's object of the same number.
			if (entry != nullptr && entry->is_compressed()) {
				const XrefEntry* container = table.find(entry->obj_stm_num);
				if (len_ref.gen_num != 0u || container == nullptr || !container->is_uncompressed()) {
					return unavailable();
				}
				RECTO_PDF_RETURN_IF_ERROR(probe_containers());
				if (const auto member = probed_members.find({container->byte_offset, entry->obj_stm_index});
					member != probed_members.end() && member->second.first == len_ref.obj_num) {
					return member->second.second;
				}
				return unavailable();
			}
			const auto it = recovered_integers.find(len_ref.obj_num);
			const bool uncompressed = it != recovered_integers.end() && it->second.first == len_ref.gen_num;
			const auto defined = defined_at.find(len_ref.obj_num);
			const uint64_t defined_pos = defined != defined_at.end() ? defined->second : 0u;
			// A container met after the object's current definition (or after its deletion by a row) replaces it, as the
			// unpack after the scan will.
			if (len_ref.gen_num == 0u && !deferred_objstms.empty() && deferred_objstms.back().pos > defined_pos) {
				RECTO_PDF_RETURN_IF_ERROR(probe_containers());
				if (const auto member = container_integers.find(len_ref.obj_num);
					member != container_integers.end() && member->second.first > defined_pos) {
					return member->second.second;
				}
			}
			if (uncompressed) {
				return it->second.second;
			}
			return unavailable();
		};

		// Linearly scan from header_off for `<N> <G> obj` and `trailer << ... >>`.
		size_t pos = static_cast<size_t>(header_off);
		while (pos < file_bytes.size()) {
			const uint8_t b = file_bytes[pos];

			// Skip PDF comment lines so `% 1 0 obj` or `% trailer` inside comments are ignored.
			if (b == '%') {
				while (pos < file_bytes.size() &&
					   file_bytes[pos] != '\n' &&
					   file_bytes[pos] != '\r') {
					++pos;
				}
				continue;
			}

			// Check for `trailer` keyword at token boundary.
			if (b == 't' && matches_token_at(file_bytes, pos, "trailer")) {
				Lexer tr_lexer(file_bytes);
				tr_lexer.seek(pos + 7u);
				Parser tr_parser(tr_lexer);
				auto tr_res = tr_parser.parse_dictionary();
				const size_t tr_scan_end = std::max(tr_lexer.max_scanned_offset(), pos + 7u);
				cumulative_parse_bytes += static_cast<uint64_t>(tr_scan_end - pos);
				if (!tr_res.has_value() && tr_res.error().code == ErrorCode::OutOfMemory) {
					return tr_res.error();
				}
				if (cumulative_parse_bytes > max_recovery_parse_bytes) {
					return make_xref_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedXRef,
						"Damaged-file recovery exceeded cumulative parse byte budget",
						pos);
				}
				if (tr_res.has_value()) {
					auto ov_res = overwrite_trailer_from_revision(table.trailer_, *tr_res);
					if (!ov_res.has_value()) {
						return ov_res.error();
					}
					pos = std::max(tr_lexer.position(), pos + 7u);
					continue;
				}
				pos += 7u;
				continue;
			}

			// Check for `<N> <G> obj` at a token boundary where `<N>` starts with an ASCII digit.
			if (b >= '0' && b <= '9' &&
				(pos == 0 || !Lexer::is_pdf_regular(file_bytes[pos - 1]))) {
				size_t p = pos;
				uint64_t obj_num_u64 = 0;
				size_t digits_n = 0;
				while (p < file_bytes.size() &&
					   file_bytes[p] >= '0' &&
					   file_bytes[p] <= '9') {
					if (digits_n <= 10u) {
						obj_num_u64 = obj_num_u64 * 10ULL + static_cast<uint64_t>(file_bytes[p] - '0');
					}
					++p;
					++digits_n;
				}
				const size_t p_after_n = p;
				const size_t p_ws1 = skip_pdf_whitespace_and_comments(file_bytes, p_after_n);
				if (digits_n >= 1u &&
					digits_n <= 10u &&
					obj_num_u64 > 0ULL &&
					obj_num_u64 <= std::numeric_limits<uint32_t>::max() &&
					p_ws1 > p_after_n &&
					p_ws1 < file_bytes.size()) {
					p = p_ws1;
					uint64_t gen_u64 = 0;
					size_t digits_g = 0;
					while (p < file_bytes.size() &&
						   file_bytes[p] >= '0' &&
						   file_bytes[p] <= '9') {
						if (digits_g <= 5u) {
							gen_u64 = gen_u64 * 10ULL + static_cast<uint64_t>(file_bytes[p] - '0');
						}
						++p;
						++digits_g;
					}
					const size_t p_after_g = p;
					const size_t p_ws2 = skip_pdf_whitespace_and_comments(file_bytes, p_after_g);
					if (digits_g >= 1u &&
						digits_g <= 5u &&
						gen_u64 <= std::numeric_limits<uint16_t>::max() &&
						p_ws2 > p_after_g) {
						p = p_ws2;
						if (matches_token_at(file_bytes, p, "obj")) {
							if (objects_discovered >= effective_max_entries ||
								candidate_attempts >= effective_max_entries) {
								return make_xref_error(
									ErrorCode::DamagedFormat,
									DiagnosticId::DamagedXRef,
									"Damaged-file recovery exceeded maximum object entry budget",
									pos);
							}
							++candidate_attempts;
							const uint32_t obj_num = static_cast<uint32_t>(obj_num_u64);
							const uint16_t gen_num = static_cast<uint16_t>(gen_u64);

							// Parse the object at `pos` to verify it is a valid indirect object
							// and to inspect its dictionary for `/Type /Catalog`, `/Type /XRef`,
							// or `/Type /ObjStm`, as well as advancing `pos` past any binary stream payload.
							Parser obj_parser(file_bytes);
							obj_parser.seek(pos);
							auto parsed_obj = obj_parser.parse_indirect_object(
								[&scan_length, obj_num](IndirectRef len_ref) -> PdfResult<int64_t> {
									return scan_length(len_ref, obj_num);
								});
							const size_t next_pos = std::max(obj_parser.lexer().position(), p + 3u);
							const size_t obj_scan_end = std::max(
								obj_parser.lexer().max_scanned_offset(),
								next_pos);
							cumulative_parse_bytes += static_cast<uint64_t>(obj_scan_end - pos);
							if (!parsed_obj.has_value()) {
								if (parsed_obj.error().code == ErrorCode::OutOfMemory ||
									parsed_obj.error().code == ErrorCode::DecompressionLimitExceeded ||
									parsed_obj.error().message ==
										"Unterminated PDF stream: missing 'endstream' keyword") {
									return parsed_obj.error();
								}
								if (cumulative_parse_bytes > max_recovery_parse_bytes) {
									return make_xref_error(
										ErrorCode::DamagedFormat,
										DiagnosticId::DamagedXRef,
										"Damaged-file recovery exceeded cumulative parse byte budget",
										pos);
								}
								pos = p + 3u;
								continue;
							}
							if (cumulative_parse_bytes > max_recovery_parse_bytes) {
								return make_xref_error(
									ErrorCode::DamagedFormat,
									DiagnosticId::DamagedXRef,
									"Damaged-file recovery exceeded cumulative parse byte budget",
									pos);
							}

							// If `obj_num` previously acted as a `/Type /ObjStm` container, redefining
							// `obj_num` invalidates all compressed objects that pointed into that container.
							remember_replaced(obj_num);
							invalidate_objstm_container(obj_num);

							if (!table.entries_.contains(obj_num)) {
								++objects_discovered;
							}
							if (obj_num > max_obj_num) {
								max_obj_num = obj_num;
							}
							// Later revisions in the file override earlier definitions of the same object.
							table.set_entry(
								obj_num,
								XrefEntry{
									XrefEntryKind::Uncompressed,
									static_cast<uint64_t>(pos),
									0u,
									0u,
									gen_num});
							defined_at[obj_num] = static_cast<uint64_t>(pos);
							if (scanned_definitions.size() < effective_max_entries) {
								scanned_definitions.push_back(ScannedDefinition{static_cast<uint64_t>(pos), obj_num, gen_num});
							}
							++definitions_made;
							recovered_integers.erase(obj_num);
							catalog_generations.erase(obj_num);
							indirect_type_dicts.erase(obj_num);
							catalog_names.erase(obj_num);
							if (info_candidate.has_value() &&
								info_candidate->obj_num == obj_num) {
								info_candidate = std::nullopt;
							}

							if (parsed_obj->value.as_name() == std::optional<std::string_view>("Catalog")) {
								catalog_names[obj_num] = gen_num;
							}
							if (const auto int_val = parsed_obj->value.as_int()) {
								recovered_integers[obj_num] = {gen_num, *int_val};
							} else if (const PdfDict* dict_only = parsed_obj->value.as_dict()) {
								const auto type_opt = dict_only->get_name("Type");
								if (const PdfValue* type_val = dict_only->get("Type");
									type_val != nullptr && type_val->is_ref()) {
									indirect_type_dicts[obj_num] = {gen_num, *type_val->as_ref()};
									catalog_history.push_back({static_cast<uint64_t>(pos), IndirectRef{obj_num, gen_num}});
								}
								if (type_opt.has_value() && *type_opt == "Catalog") {
									catalog_generations[obj_num] = gen_num;
									catalog_history.push_back({static_cast<uint64_t>(pos), IndirectRef{obj_num, gen_num}});
								} else if (
									!type_opt.has_value() &&
									(dict_only->contains("Title") ||
									 dict_only->contains("Author") ||
									 dict_only->contains("Producer") ||
									 dict_only->contains("CreationDate"))) {
									info_candidate = IndirectRef{obj_num, gen_num};
									info_candidate_pos = static_cast<uint64_t>(pos);
								}
							} else if (parsed_obj->value.is_stream()) {
								const PdfStream& stm = *parsed_obj->value.as_stream();
								const PdfDict& sdict = stm.dict;
								const auto type_opt = sdict.get_name("Type");
								if (type_opt.has_value() && *type_opt == "XRef") {
									auto ov_res = overwrite_trailer_from_revision(table.trailer_, sdict);
									if (!ov_res.has_value()) {
										return ov_res.error();
									}
									// Cross-reference streams are never encrypted (ISO 32000-1 section 7.5.8.1), so their rows say where compressed
									// objects live (type 2) and which objects a later revision deleted (type 0) even when the containers cannot be read
									// before authentication. Each row is a definition at this stream's position, under the scan's later-wins rule. A
									// stream behind an unmetered filter, or one that does not decode or unpack, contributes only its trailer keys and
									// is recorded as damage with its object number and offset.
									const auto unusable_xref_stream = [&]() {
										record_diagnostic(ErrorCode::DamagedFormat, DiagnosticId::DamagedXRef, static_cast<uint64_t>(pos), obj_num, gen_num);
									};
									if (!filters_are_metered_lossless(sdict)) {
										unusable_xref_stream();
									} else {
										// Decoded within what the cumulative budget has left, so the work stops at the budget instead of after it.
										detail::ScopedDecodeWorkMeter rows_work;
										auto rows_bytes = decode_stream(stm, static_cast<size_t>(std::min<uint64_t>(
											kMaxDecodedStreamBytes, max_recovery_decoded_objstm_bytes - cumulative_decoded_objstm_bytes)));
										if (!rows_bytes.has_value() &&
											(rows_bytes.error().code == ErrorCode::OutOfMemory ||
											 rows_bytes.error().code == ErrorCode::DecompressionLimitExceeded)) {
											return rows_bytes.error();
										}
										cumulative_decoded_objstm_bytes += std::max<uint64_t>(
											rows_work.bytes(), rows_bytes.has_value() ? static_cast<uint64_t>(rows_bytes->size()) : 0u);
										if (cumulative_decoded_objstm_bytes > max_recovery_decoded_objstm_bytes) {
											return make_xref_error(
												ErrorCode::DecompressionLimitExceeded,
												DiagnosticId::DamagedStream,
												"Damaged-file recovery exceeded cumulative decoded stream byte budget",
												pos);
										}
										XrefTable rows;
										PdfResult<void> unpacked = PdfResult<void>::ok();
										if (rows_bytes.has_value()) {
											unpacked = unpack_xref_stream(
												sdict, *rows_bytes, file_bytes.size(), header_off, rows, recovered_xref_rows, effective_max_entries);
										}
										if (!unpacked.has_value() &&
											(unpacked.error().code == ErrorCode::OutOfMemory ||
											 unpacked.error().message.view() == kXrefRowBudgetExceeded ||
											 unpacked.error().message.view() == kXrefDefaultIndexBudgetExceeded)) {
											return unpacked.error();
										}
										if (!rows_bytes.has_value() || !unpacked.has_value()) {
											unusable_xref_stream();
										} else {
											for (const auto& [member_id, row] : rows.entries_) {
												if (member_id == 0u) {
													continue;
												}
												const XrefEntry* existing = table.find(member_id);
												if (row.is_free()) {
													// A later revision deleted the object: an earlier definition goes, and an earlier container's
													// member unpacked after the scan stays out (defined_at).
													if (existing != nullptr) {
														remember_replaced(member_id);
														invalidate_objstm_container(member_id);
														table.entries_.erase(member_id);
														if (objects_discovered > 0u) {
															--objects_discovered;
														}
														recovered_integers.erase(member_id);
														catalog_generations.erase(member_id);
														indirect_type_dicts.erase(member_id);
														catalog_names.erase(member_id);
														if (info_candidate.has_value() && info_candidate->obj_num == member_id) {
															info_candidate = std::nullopt;
														}
													}
													defined_at[member_id] = static_cast<uint64_t>(pos);
													continue;
												}
												if (row.is_uncompressed()) {
													// A type-1 row is taken only for a definition the scan found (ISO 32000-1 7.5.8.3, Table 18): the one already in
													// the table, whose position it then vouches for, or an earlier one something later replaced, which it restores.
													if (existing != nullptr && existing->is_uncompressed() && existing->byte_offset == row.byte_offset &&
														existing->generation == row.generation) {
														defined_at[member_id] = std::max(defined_at[member_id], static_cast<uint64_t>(pos));
													} else if (const ScannedDefinition* seen = find_definition(row.byte_offset);
															   seen != nullptr && seen->obj_num == member_id && seen->generation == row.generation) {
														remember_replaced(member_id);
														invalidate_objstm_container(member_id);
														if (existing == nullptr) {
															if (objects_discovered >= effective_max_entries) {
																return make_xref_error(
																	ErrorCode::DamagedFormat,
																	DiagnosticId::DamagedXRef,
																	"Damaged-file recovery exceeded maximum object entry budget",
																	pos);
															}
															++objects_discovered;
														}
														table.set_entry(member_id, row);
														defined_at[member_id] = static_cast<uint64_t>(pos);
														recovered_integers.erase(member_id);
														catalog_generations.erase(member_id);
														indirect_type_dicts.erase(member_id);
														catalog_names.erase(member_id);
														if (info_candidate.has_value() && info_candidate->obj_num == member_id) {
															info_candidate = std::nullopt;
														}
														if (const auto known = definition_facts.find(row.byte_offset); known != definition_facts.end()) {
															restore_facts(member_id, known->second, static_cast<uint64_t>(pos));
														}
														max_obj_num = std::max(max_obj_num, member_id);
													}
													continue;
												}
												if (existing != nullptr && existing->is_compressed() && existing->obj_stm_num == row.obj_stm_num &&
													existing->obj_stm_index == row.obj_stm_index) {
													// The same object the scan already unpacked: what it learned (a catalog, an integer) stands.
													defined_at[member_id] = static_cast<uint64_t>(pos);
													continue;
												}
												if (existing == nullptr) {
													if (objects_discovered >= effective_max_entries) {
														return make_xref_error(
															ErrorCode::DamagedFormat,
															DiagnosticId::DamagedXRef,
															"Damaged-file recovery exceeded maximum object entry budget",
															pos);
													}
													++objects_discovered;
												}
												// A row names its container by number, not by one version of it, so it is not one of
												// objstm_members: the container's own (re)definition does not invalidate it.
												remember_replaced(member_id);
												invalidate_objstm_container(member_id);
												table.set_entry(member_id, row);
												defined_at[member_id] = static_cast<uint64_t>(pos);
												recovered_integers.erase(member_id);
												catalog_generations.erase(member_id);
												indirect_type_dicts.erase(member_id);
												catalog_names.erase(member_id);
												if (info_candidate.has_value() && info_candidate->obj_num == member_id) {
													info_candidate = std::nullopt;
												}
												if (const XrefEntry* container = table.find(row.obj_stm_num); container != nullptr && container->is_uncompressed()) {
													if (const auto known = replaced_members.find({container->byte_offset, row.obj_stm_index});
														known != replaced_members.end() && known->second.first == member_id) {
														restore_facts(member_id, known->second.second, static_cast<uint64_t>(pos));
													}
												}
												max_obj_num = std::max(max_obj_num, member_id);
											}
										}
									}
								} else if (type_opt.has_value() && *type_opt == "ObjStm" && gen_num == 0u &&
									objstm_needs_resolver(sdict)) {
									deferred_objstms.push_back(DeferredObjStm{obj_num, pos});
								} else if (type_opt.has_value() && *type_opt == "ObjStm" && gen_num == 0u &&
									!filters_are_metered_lossless(sdict)) {
									// Recovery decodes a container only through filters whose work the decode meter
									// measures, so its cumulative budget holds; an image-filtered container cannot hold
									// PDF syntax anyway. It is recorded as damage and left undecoded.
									record_diagnostic(ErrorCode::DamagedFormat, DiagnosticId::DamagedStream,
										static_cast<uint64_t>(pos), obj_num, gen_num);
								} else if (type_opt.has_value() && *type_opt == "ObjStm" && gen_num == 0u) {
									// Unpacked after the scan, in file order against the definitions around it.
									deferred_objstms.push_back(DeferredObjStm{obj_num, pos});
								}
							}

							pos = next_pos;
							continue;
						}
					}
				}
				pos = p_ws1;
				continue;
			}

			++pos;
		}

		// Unpack the deferred containers in file order, their parameters resolved one level as `parse_target` describes.
		TargetCache resolved_targets;
		const IndirectObjectResolver scan_resolver = caching_resolver(resolved_targets);
		const bool encrypted = table.trailer_.contains("Encrypt");
		for (DeferredObjStm& deferred : deferred_objstms) {
			if (encrypted) {
				break; // ciphertext until authentication; resolution decodes these containers afterwards
			}
			// A container redefined later in the file is not this object any more.
			const XrefEntry* self = table.find(deferred.obj_num);
			if (self == nullptr || !self->is_uncompressed() || self->byte_offset != deferred.pos) {
				continue;
			}
			if (const auto probed = probed_containers.find({deferred.obj_num, deferred.pos}); probed != probed_containers.end()) {
				RECTO_PDF_RETURN_IF_ERROR(unpack_objstm(deferred.obj_num, deferred.pos, probed->second.n,
					probed->second.first, std::move(probed->second.decoded)));
				continue;
			}
			// Parsed again here, through the resolver (which charges the parse budget and keeps the result), since the
			// scan kept only the position.
			auto container_value = scan_resolver(IndirectRef{deferred.obj_num, 0u});
			if (!container_value.has_value()) {
				return resolver_over_budget ? over_budget_error(deferred.pos) : container_value.error();
			}
			const PdfStream* container = container_value->as_stream();
			if (container == nullptr) {
				continue;
			}
			// The metered-filter rule above, read through the resolved /Filter.
			auto view = container_view(container->dict, scan_resolver);
			if (!view.has_value()) {
				return resolver_over_budget ? over_budget_error(deferred.pos) : view.error();
			}
			if (!filters_are_metered_lossless(view->filters)) {
				record_diagnostic(ErrorCode::DamagedFormat, DiagnosticId::DamagedStream,
					static_cast<uint64_t>(deferred.pos), deferred.obj_num, 0u);
				continue;
			}
			const std::optional<int64_t> n_opt = view->n;
			const std::optional<int64_t> first_opt = view->first;
			detail::ScopedDecodeWorkMeter decode_work;
			auto dec_stm = decode_stream(*container, static_cast<size_t>(std::min<uint64_t>(
				kMaxDecodedStreamBytes, max_recovery_decoded_objstm_bytes - cumulative_decoded_objstm_bytes)),
				&scan_resolver);
			if (resolver_over_budget) {
				return over_budget_error(deferred.pos);
			}
			if (!dec_stm.has_value() &&
				(dec_stm.error().code == ErrorCode::OutOfMemory ||
				 dec_stm.error().code == ErrorCode::DecompressionLimitExceeded)) {
				return dec_stm.error();
			}
			cumulative_decoded_objstm_bytes += std::max<uint64_t>(
				decode_work.bytes(),
				dec_stm.has_value() ? static_cast<uint64_t>(dec_stm->size()) : 0u);
			if (cumulative_decoded_objstm_bytes > max_recovery_decoded_objstm_bytes) {
				return make_xref_error(
					ErrorCode::DecompressionLimitExceeded,
					DiagnosticId::DamagedStream,
					"Damaged-file recovery exceeded cumulative decoded /ObjStm byte budget",
					deferred.pos);
			}
			if (dec_stm.has_value()) {
				RECTO_PDF_RETURN_IF_ERROR(
					unpack_objstm(deferred.obj_num, deferred.pos, n_opt, first_opt, std::move(*dec_stm)));
			}
		}

		// A dictionary whose /Type is an indirect reference is a catalog when the object that reference names (one
		// level, never following a further reference) is the name /Catalog (ISO 32000-1 section 7.3.10). The scan
		// already parsed every object once and recorded those names (catalog_names), so this step parses nothing and
		// recovery stays linear. The dictionaries already sit in catalog_history with their file positions, so the
		// fallback below keeps preferring the latest; here they only become valid.
		for (const auto& [dict_num, gen_and_type] : indirect_type_dicts) {
			const IndirectRef type_ref = gen_and_type.second;
			if (const auto named = catalog_names.find(type_ref.obj_num);
				named != catalog_names.end() && named->second == type_ref.gen_num) {
				catalog_generations[dict_num] = gen_and_type.first;
			}
		}

		// Validate that a candidate `/Root` reference resolves with matching
		// generation to a dictionary whose `/Type` is `/Catalog` using the
		// single-pass `catalog_generations` map (zero secondary reparsing).
		// An encrypted file's containers cannot be read before authentication, so a /Root naming a compressed object
		// an intact xref stream lists cannot be checked here; it is taken on that stream's word, and resolution checks
		// it after authentication.
		auto is_valid_catalog_ref = [&catalog_generations, &table, encrypted](IndirectRef ref) noexcept -> bool {
			const auto it = catalog_generations.find(ref.obj_num);
			if (it != catalog_generations.end() && it->second == ref.gen_num) {
				return true;
			}
			const XrefEntry* entry = table.find(ref.obj_num);
			return encrypted && ref.gen_num == 0u && entry != nullptr && entry->is_compressed();
		};

		auto has_valid_root_entry = [&table, &is_valid_catalog_ref]() noexcept -> bool {
			const auto root_opt = table.trailer_.get_ref("Root");
			return root_opt.has_value() && is_valid_catalog_ref(*root_opt);
		};

		if (!has_valid_root_entry()) {
			// The latest valid candidate in the file wins; among equal positions (members of one container), the one
			// recorded last, as before.
			const std::pair<uint64_t, IndirectRef>* latest = nullptr;
			for (const auto& candidate : catalog_history) {
				if (is_valid_catalog_ref(candidate.second) && (latest == nullptr || candidate.first >= latest->first)) {
					latest = &candidate;
				}
			}
			if (latest != nullptr) {
				table.trailer_.set("Root", PdfValue(latest->second));
			}
		}
		if (!table.trailer_.contains("Info") && info_candidate.has_value()) {
			table.trailer_.set("Info", PdfValue(*info_candidate));
		}
		if (!table.trailer_.contains("Size")) {
			table.trailer_.set(
				"Size",
				PdfValue(static_cast<int64_t>(static_cast<uint64_t>(max_obj_num) + 1ULL)));
		}

		if (!has_valid_root_entry() || objects_discovered == 0u) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedXRef,
				"Damaged-file recovery could not locate any valid /Type /Catalog root object",
				header_off);
		}

		return table;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while recovering damaged cross-reference table");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while recovering damaged cross-reference table");
	}
}

PdfResult<XrefTable> XrefTable::parse(
	std::span<const uint8_t> file_bytes,
	uint64_t max_total_entries,
	bool allow_recovery) noexcept
{
	try {
		const uint64_t effective_max_entries =
			std::min<uint64_t>(max_total_entries, kMaxXrefTotalEntries);
		std::string version_str;
		RECTO_PDF_TRY(const uint64_t header_off, locate_header_offset(file_bytes, &version_str));

		auto try_recover = [&](PdfError primary_err) -> PdfResult<XrefTable> {
			if (!allow_recovery ||
				primary_err.code == ErrorCode::OutOfMemory ||
				primary_err.code == ErrorCode::DecompressionLimitExceeded) {
				return primary_err;
			}
			auto rec = recover_damaged_xref(file_bytes, effective_max_entries);
			if (rec.has_value() ||
				rec.error().code == ErrorCode::OutOfMemory ||
				rec.error().code == ErrorCode::DecompressionLimitExceeded) {
				return rec;
			}
			return primary_err;
		};

		auto startxref_res = locate_startxref_offset(file_bytes);
		if (!startxref_res.has_value()) {
			return try_recover(std::move(startxref_res).error());
		}
		const uint64_t raw_startxref = *startxref_res;

		if (raw_startxref > file_bytes.size() - header_off ||
			raw_startxref + header_off >= file_bytes.size()) {
			return try_recover(make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedXRef,
				"startxref byte offset points outside file bounds",
				raw_startxref));
		}

		XrefTable table;
		table.header_offset_ = header_off;
		table.pdf_version_ = std::move(version_str);

		std::unordered_set<uint64_t> visited_offsets;
		uint64_t current_xref_offset = raw_startxref + header_off;
		uint64_t total_rows_parsed = 0;

		while (true) {
			if (table.revision_count_ >= kMaxXrefRevisions) {
				return make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedXRef,
					"Cross-reference /Prev chain exceeds maximum 256 revisions",
					current_xref_offset);
			}
			if (!visited_offsets.insert(current_xref_offset).second) {
				return make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedXRef,
					"Cyclic /Prev cross-reference offset detected",
					current_xref_offset);
			}
			++table.revision_count_;

			Lexer lexer(file_bytes);
			lexer.seek(static_cast<size_t>(current_xref_offset));

			Token kw_tok;
			PdfError err;
			if (!next_non_comment_token(lexer, kw_tok, err)) {
				return try_recover(std::move(err));
			}

			PdfDict rev_trailer;
			if (kw_tok.is_keyword("xref")) {
				// Classic ASCII `xref` table followed by `trailer << ... >>`.
				// Buffer this revision's classic entries so any supplemental `/XRefStm`
				// in the same revision overrides classic compatibility entries (such as
				// free placeholders for compressed objects per ISO 32000-1 7.5.8.4)
				// while preserving entries from newer revisions.
				std::vector<std::pair<uint32_t, XrefEntry>> classic_rev_entries;
				// In-use rows whose offset lies outside the file, checked once the trailer's /Size is known: a row
				// numbered above /Size is missing (ISO 32000-1 Table 15), so its offset cannot fail the parse.
				struct OutOfBoundsRow {
					uint32_t obj_num;
					uint16_t gen_num;
					uint64_t row_offset;
				};
				std::vector<OutOfBoundsRow> out_of_bounds_rows;
				while (true) {
					Token first_tok;
					if (!next_non_comment_token(lexer, first_tok, err)) {
						return try_recover(std::move(err));
					}
					if (first_tok.is_keyword("trailer")) {
						break;
					}
					if (first_tok.kind != TokenKind::Integer ||
						first_tok.int_value < 0 ||
						first_tok.int_value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
						return try_recover(make_xref_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedXRef,
							"Invalid first object number in xref subsection header",
							first_tok.byte_offset));
					}

					Token count_tok;
					if (!next_non_comment_token(lexer, count_tok, err)) {
						return try_recover(std::move(err));
					}
					if (count_tok.kind != TokenKind::Integer ||
						count_tok.int_value < 0 ||
						count_tok.int_value > static_cast<int64_t>(kMaxXrefSubsectionCount)) {
						return try_recover(make_xref_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedXRef,
							"Invalid or excessive object count in xref subsection header",
							count_tok.byte_offset));
					}

					const uint64_t first_id = static_cast<uint64_t>(first_tok.int_value);
					const uint64_t count = static_cast<uint64_t>(count_tok.int_value);
					if (first_id + count >
						static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1ULL) {
						return try_recover(make_xref_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedXRef,
							"xref subsection object range overflows 32-bit object numbers",
							first_tok.byte_offset));
					}
					if (count > effective_max_entries - total_rows_parsed) {
						return make_xref_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedXRef,
							"Cumulative xref entry count across subsections exceeds maximum budget",
							count_tok.byte_offset);
					}
					total_rows_parsed += count;

					for (uint64_t i = 0; i < count; ++i) {
						Token off_tok;
						Token gen_tok;
						Token flag_tok;
						if (!next_non_comment_token(lexer, off_tok, err) ||
							!next_non_comment_token(lexer, gen_tok, err) ||
							!next_non_comment_token(lexer, flag_tok, err)) {
							return try_recover(std::move(err));
						}

						if (off_tok.kind != TokenKind::Integer || off_tok.int_value < 0) {
							return try_recover(make_xref_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedXRef,
								"Invalid byte offset in classic xref entry",
								off_tok.byte_offset));
						}
						if (gen_tok.kind != TokenKind::Integer ||
							gen_tok.int_value < 0 ||
							gen_tok.int_value > static_cast<int64_t>(std::numeric_limits<uint16_t>::max())) {
							return try_recover(make_xref_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedXRef,
								"Invalid generation number in classic xref entry",
								gen_tok.byte_offset));
						}
						if (flag_tok.kind != TokenKind::Keyword ||
							(flag_tok.text != "n" && flag_tok.text != "f")) {
							return try_recover(make_xref_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedXRef,
								"Expected 'n' or 'f' status flag in classic xref entry",
								flag_tok.byte_offset));
						}

						const uint32_t obj_num = static_cast<uint32_t>(first_id + i);
						const uint16_t gen_num = static_cast<uint16_t>(gen_tok.int_value);
						const uint64_t raw_off = static_cast<uint64_t>(off_tok.int_value);

						if (flag_tok.text == "n") {
							if (obj_num == 0) {
								classic_rev_entries.emplace_back(
									0u,
									XrefEntry{XrefEntryKind::Free, raw_off, 0, 0, gen_num});
								continue;
							}
							if (raw_off > file_bytes.size() - header_off ||
								raw_off + header_off >= file_bytes.size()) {
								out_of_bounds_rows.push_back({obj_num, gen_num, off_tok.byte_offset});
								continue;
							}
							classic_rev_entries.emplace_back(
								obj_num,
								XrefEntry{
									XrefEntryKind::Uncompressed,
									raw_off + header_off,
									0,
									0,
									gen_num});
						} else {
							classic_rev_entries.emplace_back(
								obj_num,
								XrefEntry{
									XrefEntryKind::Free,
									raw_off,
									0,
									0,
									gen_num});
						}
					}
				}

				Parser dict_parser(lexer);
				auto trailer_res = dict_parser.parse_dictionary();
				if (!trailer_res.has_value()) {
					PdfError tr_err = std::move(trailer_res).error();
					if (tr_err.code == ErrorCode::DamagedFormat) {
						record_diagnostic(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedTrailer,
							tr_err.byte_offset);
					}
					return try_recover(std::move(tr_err));
				}
				rev_trailer = std::move(*trailer_res);
				auto merge_res = merge_trailer_from_revision(table.trailer_, rev_trailer);
				if (!merge_res.has_value()) {
					return merge_res.error();
				}
				if (!out_of_bounds_rows.empty()) {
					const auto merged_size = table.trailer_.get_int("Size");
					for (const OutOfBoundsRow& row : out_of_bounds_rows) {
						if (merged_size.has_value() && *merged_size > 0 &&
							static_cast<uint64_t>(row.obj_num) > static_cast<uint64_t>(*merged_size)) {
							continue; // above /Size: missing, so its offset does not matter
						}
						return try_recover(make_xref_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedXRef,
							"In-use xref entry offset points outside file bounds",
							row.row_offset,
							row.obj_num,
							row.gen_num));
					}
				}

				// Hybrid cross-reference stream (`/XRefStm` in a classic trailer, ISO 32000-1 7.5.8.4):
				// unpack entries from the supplemental `/Type /XRef` stream before inserting the
				// buffered classic entries of the same revision so `/XRefStm` overrides same-revision
				// classic compatibility entries while newer revisions remain preserved.
				if (const PdfValue* xref_stm_val = rev_trailer.get("XRefStm")) {
					const auto stm_off_opt = xref_stm_val->as_int();
					if (!stm_off_opt.has_value() || *stm_off_opt < 0) {
						return try_recover(make_xref_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedTrailer,
							"Invalid /XRefStm offset in trailer dictionary",
							current_xref_offset));
					}
					const uint64_t raw_stm_off = static_cast<uint64_t>(*stm_off_opt);
					if (raw_stm_off > file_bytes.size() - header_off ||
						raw_stm_off + header_off >= file_bytes.size()) {
						return try_recover(make_xref_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedXRef,
							"/XRefStm offset points outside file bounds",
							current_xref_offset));
					}
					const uint64_t adjusted_stm_off = raw_stm_off + header_off;
					if (!visited_offsets.insert(adjusted_stm_off).second) {
						return make_xref_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedXRef,
							"Cyclic /XRefStm cross-reference offset detected",
							adjusted_stm_off);
					}

					auto stm_obj_res = parse_xref_stream_object_at(file_bytes, adjusted_stm_off);
					if (!stm_obj_res.has_value()) {
						return try_recover(std::move(stm_obj_res).error());
					}
					const PdfStream* xstm = stm_obj_res->value.as_stream();
					auto dec_res = decode_stream(*xstm);
					if (!dec_res.has_value()) {
						if (dec_res.error().code == ErrorCode::DamagedFormat) {
							record_diagnostic(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedXRef,
								adjusted_stm_off);
						}
						return try_recover(std::move(dec_res).error());
					}
					auto unpack_res = unpack_xref_stream(
						xstm->dict,
						*dec_res,
						file_bytes.size(),
						header_off,
						table,
						total_rows_parsed,
						effective_max_entries);
					if (!unpack_res.has_value()) {
						return try_recover(std::move(unpack_res).error());
					}
				}

				for (const auto& [c_obj_num, c_entry] : classic_rev_entries) {
					table.entries_.try_emplace(c_obj_num, c_entry);
				}
			} else if (kw_tok.kind == TokenKind::Integer && kw_tok.int_value > 0) {
				// PDF 1.5+ `/Type /XRef` cross-reference stream object `<N> <G> obj << /Type /XRef ... >> stream`.
				auto stm_obj_res = parse_xref_stream_object_at(file_bytes, current_xref_offset);
				if (!stm_obj_res.has_value()) {
					return try_recover(std::move(stm_obj_res).error());
				}
				const PdfStream* xstm = stm_obj_res->value.as_stream();
				auto dec_res = decode_stream(*xstm);
				if (!dec_res.has_value()) {
					if (dec_res.error().code == ErrorCode::DamagedFormat) {
						record_diagnostic(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedXRef,
							current_xref_offset,
							stm_obj_res->ref.obj_num,
							stm_obj_res->ref.gen_num);
					}
					return try_recover(std::move(dec_res).error());
				}
				auto unpack_res = unpack_xref_stream(
					xstm->dict,
					*dec_res,
					file_bytes.size(),
					header_off,
					table,
					total_rows_parsed,
					effective_max_entries);
				if (!unpack_res.has_value()) {
					return try_recover(std::move(unpack_res).error());
				}
				RECTO_PDF_TRY(PdfValue cloned_dict_val, PdfValue(xstm->dict).deep_clone());
				rev_trailer = std::move(*cloned_dict_val.as_dict());
			} else {
				return try_recover(make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedXRef,
					"Expected 'xref' keyword or indirect /Type /XRef stream object at cross-reference offset",
					current_xref_offset));
			}

			// Follow `/Prev` incremental update chain if present.
			const PdfValue* prev_val = rev_trailer.get("Prev");
			if (prev_val == nullptr) {
				break;
			}
			const auto prev_int = prev_val->as_int();
			if (!prev_int.has_value() || *prev_int < 0) {
				return try_recover(make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedTrailer,
					"Invalid /Prev offset in trailer dictionary",
					current_xref_offset));
			}
			const uint64_t raw_prev = static_cast<uint64_t>(*prev_int);
			if (raw_prev > file_bytes.size() - header_off ||
				raw_prev + header_off >= file_bytes.size()) {
				return try_recover(make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedXRef,
					"/Prev cross-reference offset points outside file bounds",
					current_xref_offset));
			}
			current_xref_offset = raw_prev + header_off;
		}

		if (!table.trailer_.contains("Root")) {
			return try_recover(make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedTrailer,
				"Trailer dictionary is missing required /Root catalog reference",
				raw_startxref + header_off));
		}

		// ISO 32000-1 Table 15: /Size is required, and an object in a cross-reference section whose number is greater
		// than it shall be ignored and treated as missing (kept in ISO 32000-2 and its errata).
		const auto trailer_size = table.trailer_.get_int("Size");
		if (!trailer_size.has_value() || *trailer_size <= 0 ||
			static_cast<uint64_t>(*trailer_size) >
				static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1ULL) {
			return try_recover(make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedTrailer,
				"Trailer dictionary is missing a valid /Size",
				raw_startxref + header_off));
		}
		const uint64_t size_limit = static_cast<uint64_t>(*trailer_size);
		for (auto it = table.entries_.begin(); it != table.entries_.end();) {
			if (static_cast<uint64_t>(it->first) > size_limit) {
				record_diagnostic(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedXRef,
					std::nullopt,
					it->first,
					0);
				it = table.entries_.erase(it);
			} else {
				++it;
			}
		}

		return table;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while parsing cross-reference table");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while parsing cross-reference table");
	}
}

bool XrefTable::contains(uint32_t obj_num) const noexcept
{
	return entries_.find(obj_num) != entries_.end();
}

const XrefEntry* XrefTable::find(uint32_t obj_num) const noexcept
{
	const auto it = entries_.find(obj_num);
	if (it == entries_.end()) {
		return nullptr;
	}
	return &it->second;
}

std::optional<IndirectRef> XrefTable::root_ref() const noexcept
{
	return trailer_.get_ref("Root");
}

std::optional<IndirectRef> XrefTable::info_ref() const noexcept
{
	return trailer_.get_ref("Info");
}

std::optional<IndirectRef> XrefTable::encrypt_ref() const noexcept
{
	return trailer_.get_ref("Encrypt");
}

void XrefTable::set_entry(uint32_t obj_num, XrefEntry entry)
{
	entries_[obj_num] = entry;
	obj_stm_cache_.erase(obj_num);
}

PdfResult<IndirectObject> XrefTable::read_indirect_object(
	std::span<const uint8_t> file_bytes,
	IndirectRef ref) const noexcept
{
	try {
		std::vector<IndirectRef> visited_refs;
		visited_refs.reserve(kMaxIndirectRefDepth);
		visited_refs.push_back(ref);
		return read_indirect_object_with_visited(file_bytes, ref, visited_refs);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while reading indirect object",
			std::nullopt,
			ref.obj_num,
			ref.gen_num);
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while reading indirect object",
			std::nullopt,
			ref.obj_num,
			ref.gen_num);
	}
}

PdfResult<IndirectObject> XrefTable::read_compressed_object_with_visited(
	std::span<const uint8_t> file_bytes,
	IndirectRef ref,
	const XrefEntry& entry,
	std::vector<IndirectRef>& visited_refs) const
{
	// Per ISO 32000-1 section 7.5.7, compressed objects always have generation 0.
	if (ref.gen_num != 0u) {
		return IndirectObject{ref, PdfValue(PdfNull{})};
	}

	const uint32_t stm_obj_num = entry.obj_stm_num;
	const uint32_t stm_index = entry.obj_stm_index;

	if (stm_obj_num == 0u || stm_obj_num == ref.obj_num) {
		return make_xref_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedObject,
			"Invalid or self-referential /ObjStm object number",
			std::nullopt,
			ref.obj_num,
			ref.gen_num);
	}

	const XrefEntry* stm_entry = find(stm_obj_num);
	if (stm_entry == nullptr || stm_entry->is_free()) {
		return make_xref_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedObject,
			"Referenced /Type /ObjStm object is missing or free in xref table",
			std::nullopt,
			stm_obj_num,
			0u);
	}
	// Refuse nested `/ObjStm -> /ObjStm` (a type-2 XRef entry pointing to an
	// object that is itself type-2) per ISO 32000-1 section 7.5.7.
	if (!stm_entry->is_uncompressed() || stm_entry->generation != 0u) {
		return make_xref_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedObject,
			"Nested /ObjStm (compressed object stream stored inside another /ObjStm) is forbidden",
			std::nullopt,
			stm_obj_num,
			stm_entry->generation);
	}

	const CachedObjStream* cached_stm = obj_stm_cache_.find(stm_obj_num);
	CachedObjStream uncached; // holds a container too large for the cache, for this one read
	if (cached_stm == nullptr) {
		const IndirectRef stm_ref{stm_obj_num, 0u};
		if (std::find(visited_refs.begin(), visited_refs.end(), stm_ref) != visited_refs.end()) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"Cyclic reference while resolving /Type /ObjStm stream",
				stm_entry->byte_offset,
				stm_obj_num,
				0u);
		}
		if (visited_refs.size() >= kMaxIndirectRefDepth) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"Indirect reference depth exceeded while resolving /Type /ObjStm stream",
				stm_entry->byte_offset,
				stm_obj_num,
				0u);
		}
		visited_refs.push_back(stm_ref);
		auto stm_obj_res = read_indirect_object_with_visited(file_bytes, stm_ref, visited_refs);
		visited_refs.pop_back();
		if (!stm_obj_res.has_value()) {
			return stm_obj_res.error();
		}
		if (!stm_obj_res->value.is_stream()) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"Target of type-2 XRef entry is not a stream object",
				stm_entry->byte_offset,
				stm_obj_num,
				0u);
		}

		const PdfStream& obj_stm = *stm_obj_res->value.as_stream();
		// The container's /Type, /Filter, /DecodeParms, /N and /First may be indirect; they resolve with this stream already visited, so
		// a parameter pointing back into it is a cycle rather than a recursion.
		std::vector<IndirectRef> parms_visited = visited_refs;
		parms_visited.push_back(stm_ref);
		const IndirectObjectResolver parms_resolver = [&](IndirectRef ref) -> PdfResult<PdfValue> {
			std::vector<IndirectRef> hop_visited = parms_visited;
			RECTO_PDF_TRY_REF(target, read_indirect_object_with_visited(file_bytes, ref, hop_visited));
			return std::move(target.value);
		};
		RECTO_PDF_TRY(const bool is_objstm, is_object_stream(obj_stm.dict, &parms_resolver));
		if (!is_objstm) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"Compressed object container stream is missing /Type /ObjStm",
				stm_entry->byte_offset,
				stm_obj_num,
				0u);
		}

		auto decoded_res = decode_stream(obj_stm, kMaxDecodedStreamBytes, &parms_resolver);
		if (!decoded_res.has_value()) {
			if (decoded_res.error().code == ErrorCode::DamagedFormat) {
				record_diagnostic(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					stm_entry->byte_offset,
					stm_obj_num,
					0u);
			}
			return decoded_res.error();
		}

		RECTO_PDF_TRY_REF(
			cached,
			parse_object_stream(obj_stm.dict, std::move(*decoded_res), stm_obj_num, &parms_resolver));
		cached_stm = &obj_stm_cache_.insert(stm_obj_num, std::move(cached), uncached);
	}

	RECTO_PDF_TRY_REF(member, extract_object_stream_member(*cached_stm, stm_index, ref));
	return IndirectObject{ref, std::move(member)};
}

namespace detail {

thread_local int g_objstm_cache_alloc_fail_countdown = -1;

void set_objstm_cache_alloc_fail_countdown(int countdown) noexcept
{
	g_objstm_cache_alloc_fail_countdown = countdown;
}

void check_objstm_cache_alloc()
{
	if (g_objstm_cache_alloc_fail_countdown < 0) {
		return;
	}
	if (g_objstm_cache_alloc_fail_countdown == 0) {
		g_objstm_cache_alloc_fail_countdown = -1;
		throw std::bad_alloc{};
	}
	--g_objstm_cache_alloc_fail_countdown;
}

} // namespace detail

const CachedObjStream& ObjStmCache::insert(uint32_t stm_obj_num, CachedObjStream stm, CachedObjStream& overflow)
{
	const size_t stm_bytes = stm.decoded_bytes.size();
	if (stm_bytes > kMaxCachedObjStmBytes) {
		overflow = std::move(stm);
		return overflow;
	}
	if (const auto it = by_num_.find(stm_obj_num); it != by_num_.end()) {
		bytes_ -= it->second.decoded_bytes.size();
		by_num_.erase(it);
		order_.erase(std::find(order_.begin(), order_.end(), stm_obj_num));
	}
	while (!order_.empty() && (by_num_.size() >= kMaxCachedObjStreams || bytes_ + stm_bytes > kMaxCachedObjStmBytes)) {
		if (const auto oldest = by_num_.find(order_.front()); oldest != by_num_.end()) {
			bytes_ -= oldest->second.decoded_bytes.size();
			by_num_.erase(oldest);
		}
		order_.erase(order_.begin());
	}
	// Both allocations happen before any bookkeeping changes: if either throws, the cache is as it was after the
	// evictions above, which keep order_, by_num_ and bytes_ in step.
	detail::check_objstm_cache_alloc();
	order_.reserve(order_.size() + 1u);
	detail::check_objstm_cache_alloc();
	auto& stored = by_num_.emplace(stm_obj_num, std::move(stm)).first->second;
	order_.push_back(stm_obj_num); // cannot reallocate: capacity reserved above
	bytes_ += stm_bytes;
	return stored;
}

PdfResult<bool> is_object_stream(const PdfDict& stm_dict, const IndirectObjectResolver* resolver) noexcept
{
	try {
		const PdfValue* type = stm_dict.get("Type");
		if (type == nullptr) {
			return false;
		}
		if (const auto ref = type->as_ref(); ref.has_value() && resolver != nullptr && *resolver) {
			RECTO_PDF_TRY_REF(target, (*resolver)(*ref));
			return target.as_name() == std::optional<std::string_view>("ObjStm");
		}
		return type->as_name() == std::optional<std::string_view>("ObjStm");
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory while reading an object stream's /Type");
	} catch (...) {
		return make_pdf_error(ErrorCode::InternalError, "Unexpected exception while reading an object stream's /Type");
	}
}

PdfResult<CachedObjStream> parse_object_stream(
	const PdfDict& stm_dict,
	std::vector<uint8_t> decoded_bytes,
	uint32_t stm_obj_num,
	const IndirectObjectResolver* resolver) noexcept
{
	try {
		// /N and /First may be indirect references to uncompressed objects (ISO 32000-1 section 7.3.10).
		const auto read_int = [&](std::string_view key) -> PdfResult<std::optional<int64_t>> {
			const PdfValue* v = stm_dict.get(key);
			if (v == nullptr) {
				return std::optional<int64_t>{};
			}
			if (const auto ref = v->as_ref(); ref.has_value() && resolver != nullptr && *resolver) {
				RECTO_PDF_TRY_REF(target, (*resolver)(*ref));
				return target.as_int();
			}
			return v->as_int();
		};
		RECTO_PDF_TRY(const std::optional<int64_t> n_opt, read_int("N"));
		RECTO_PDF_TRY(const std::optional<int64_t> first_opt, read_int("First"));
		if (!n_opt.has_value() || *n_opt <= 0 || *n_opt > static_cast<int64_t>(kMaxObjStmEntries) ||
			!first_opt.has_value() || *first_opt < 0) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"Invalid or missing /N or /First in /Type /ObjStm dictionary",
				std::nullopt,
				stm_obj_num,
				0u);
		}
		const uint64_t first_off = static_cast<uint64_t>(*first_opt);
		if (first_off > decoded_bytes.size()) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"/Type /ObjStm /First offset exceeds decoded stream length",
				std::nullopt,
				stm_obj_num,
				0u);
		}

		const uint32_t n_count = static_cast<uint32_t>(*n_opt);
		CachedObjStream cached;
		cached.first_offset = first_off;
		cached.entries.reserve(n_count);
		// The `/N` pairs of `<obj_num> <relative_offset>` come only from `[0, first_off)`, so a malformed header
		// cannot bleed into object bodies.
		Lexer hdr_lexer(std::span<const uint8_t>(decoded_bytes.data(), static_cast<size_t>(first_off)));
		for (uint32_t i = 0; i < n_count; ++i) {
			Token num_tok;
			Token off_tok;
			PdfError herr;
			if (!next_non_comment_token(hdr_lexer, num_tok, herr) ||
				!next_non_comment_token(hdr_lexer, off_tok, herr)) {
				if (herr.code == ErrorCode::OutOfMemory) {
					return herr;
				}
				return make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedObject,
					"Truncated or malformed header table in /Type /ObjStm stream",
					std::nullopt,
					stm_obj_num,
					0u);
			}
			if (num_tok.kind != TokenKind::Integer || num_tok.int_value <= 0 ||
				num_tok.int_value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) ||
				off_tok.kind != TokenKind::Integer || off_tok.int_value < 0) {
				return make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedObject,
					"Invalid object number or relative offset in /Type /ObjStm header",
					std::nullopt,
					stm_obj_num,
					0u);
			}
			const uint64_t rel_off = static_cast<uint64_t>(off_tok.int_value);
			if (rel_off > decoded_bytes.size() - first_off ||
				(!cached.entries.empty() && rel_off < cached.entries.back().rel_offset)) {
				return make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedObject,
					"/Type /ObjStm entry relative offset is out of bounds or non-monotonic",
					std::nullopt,
					stm_obj_num,
					0u);
			}
			cached.entries.push_back(CachedObjStream::HeaderPair{static_cast<uint32_t>(num_tok.int_value), rel_off});
		}
		cached.decoded_bytes = std::move(decoded_bytes);
		return cached;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory while parsing /Type /ObjStm header");
	} catch (...) {
		return make_pdf_error(ErrorCode::InternalError, "Unexpected exception while parsing /Type /ObjStm header");
	}
}

PdfResult<PdfValue> extract_object_stream_member(
	const CachedObjStream& stm,
	uint32_t index,
	IndirectRef ref) noexcept
{
	try {
		if (index >= stm.entries.size()) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"Compressed object index exceeds /Type /ObjStm /N entry count",
				std::nullopt,
				ref.obj_num,
				ref.gen_num);
		}
		const CachedObjStream::HeaderPair& pair = stm.entries[index];
		if (pair.obj_num != ref.obj_num) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"Compressed object number in /Type /ObjStm header does not match xref entry",
				std::nullopt,
				ref.obj_num,
				ref.gen_num);
		}
		const size_t obj_start = static_cast<size_t>(stm.first_offset + pair.rel_offset);
		const size_t obj_end = index + 1u < stm.entries.size()
			? static_cast<size_t>(stm.first_offset + stm.entries[index + 1u].rel_offset)
			: stm.decoded_bytes.size();
		Parser inner_parser(std::span<const uint8_t>(stm.decoded_bytes.data() + obj_start, obj_end - obj_start));
		auto val_res = inner_parser.parse_direct_object();
		if (!val_res.has_value()) {
			if (val_res.error().code == ErrorCode::DamagedFormat) {
				record_diagnostic(ErrorCode::DamagedFormat, DiagnosticId::DamagedObject, std::nullopt, ref.obj_num,
					ref.gen_num);
			}
			return val_res.error();
		}
		// ISO 32000-1 section 7.5.7: each slice holds a single direct object, with no `N G obj` header, `stream`
		// payload, `endobj` footer or trailing tokens, and is never a bare object reference.
		Token trailing_tok;
		PdfError terr;
		if (!next_non_comment_token(inner_parser.lexer(), trailing_tok, terr)) {
			if (terr.code == ErrorCode::DamagedFormat) {
				record_diagnostic(ErrorCode::DamagedFormat, DiagnosticId::DamagedObject, std::nullopt, ref.obj_num,
					ref.gen_num);
			}
			return terr;
		}
		if (!trailing_tok.is_eof()) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"Indirect stream, obj header, or trailing token is forbidden inside /Type /ObjStm",
				std::nullopt,
				ref.obj_num,
				ref.gen_num);
		}
		if (val_res->is_ref()) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"A compressed object cannot consist solely of an object reference (ISO 32000-1 section 7.5.7)",
				std::nullopt,
				ref.obj_num,
				ref.gen_num);
		}
		if (const PdfDict* inner_dict = val_res->as_dict()) {
			if (const auto inner_type = inner_dict->get_name("Type")) {
				if (*inner_type == "ObjStm" || *inner_type == "XRef") {
					return make_xref_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedObject,
						"/Type /ObjStm or /Type /XRef object cannot be stored inside an /ObjStm",
						std::nullopt,
						ref.obj_num,
						ref.gen_num);
				}
			}
		}
		return std::move(*val_res);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory while extracting a /Type /ObjStm member");
	} catch (...) {
		return make_pdf_error(ErrorCode::InternalError, "Unexpected exception while extracting a /Type /ObjStm member");
	}
}

PdfResult<IndirectObject> XrefTable::read_indirect_object_with_visited(
	std::span<const uint8_t> file_bytes,
	IndirectRef ref,
	std::vector<IndirectRef>& visited_refs) const
{
	if (ref.obj_num == 0) {
		return make_xref_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedObject,
			"Object number 0 cannot be resolved as an indirect object",
			std::nullopt,
			ref.obj_num,
			ref.gen_num);
	}

	const XrefEntry* entry = find(ref.obj_num);
	if (entry == nullptr || entry->is_free()) {
		return IndirectObject{ref, PdfValue(PdfNull{})};
	}
	if (entry->is_compressed()) {
		return read_compressed_object_with_visited(file_bytes, ref, *entry, visited_refs);
	}
	if (entry->generation != ref.gen_num) {
		return IndirectObject{ref, PdfValue(PdfNull{})};
	}
	if (entry->byte_offset >= file_bytes.size()) {
		return make_xref_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedXRef,
			"Cross-reference byte offset exceeds file size",
			entry->byte_offset,
			ref.obj_num,
			ref.gen_num);
	}

	Parser parser(file_bytes);
	parser.seek(static_cast<size_t>(entry->byte_offset));

	std::optional<PdfError> length_resolve_error;
	auto obj_res = parser.parse_indirect_object(
		[this, file_bytes, &visited_refs, &length_resolve_error](
			IndirectRef len_ref) -> PdfResult<int64_t> {
			auto len_res = resolve_object_with_visited(file_bytes, len_ref, visited_refs);
			if (!len_res.has_value()) {
				length_resolve_error = len_res.error();
				return std::move(len_res).error();
			}
			const auto len_int = len_res->as_int();
			if (!len_int.has_value()) {
				return make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Indirect stream /Length object is not an integer",
					std::nullopt,
					len_ref.obj_num,
					len_ref.gen_num);
			}
			return *len_int;
		});
	if (length_resolve_error.has_value()) {
		return std::move(*length_resolve_error);
	}
	if (!obj_res.has_value()) {
		return obj_res;
	}

	if (obj_res->ref.obj_num != ref.obj_num || obj_res->ref.gen_num != ref.gen_num) {
		return make_xref_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedObject,
			"Indirect object header ID/generation does not match xref entry",
			entry->byte_offset,
			ref.obj_num,
			ref.gen_num);
	}
	return obj_res;
}

PdfResult<PdfValue> XrefTable::resolve_object(
	std::span<const uint8_t> file_bytes,
	IndirectRef ref) const noexcept
{
	try {
		std::vector<IndirectRef> visited_refs;
		visited_refs.reserve(kMaxIndirectRefDepth);
		return resolve_object_with_visited(file_bytes, ref, visited_refs);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while resolving indirect object",
			std::nullopt,
			ref.obj_num,
			ref.gen_num);
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while resolving indirect object",
			std::nullopt,
			ref.obj_num,
			ref.gen_num);
	}
}

PdfResult<PdfValue> XrefTable::resolve_object_with_visited(
	std::span<const uint8_t> file_bytes,
	IndirectRef ref,
	std::vector<IndirectRef>& visited_refs) const
{
	struct VisitedScopeGuard {
		std::vector<IndirectRef>& vec;
		size_t saved_size;
		~VisitedScopeGuard() noexcept
		{
			vec.resize(saved_size);
		}
	} guard{visited_refs, visited_refs.size()};

	IndirectRef current = ref;
	while (true) {
		if (std::find(visited_refs.begin(), visited_refs.end(), current) != visited_refs.end()) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"Cyclic indirect object reference chain detected",
				std::nullopt,
				current.obj_num,
				current.gen_num);
		}
		if (visited_refs.size() >= kMaxIndirectRefDepth) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"Indirect object reference chain exceeds maximum depth of 32",
				std::nullopt,
				current.obj_num,
				current.gen_num);
		}
		visited_refs.push_back(current);

		RECTO_PDF_TRY(
			IndirectObject obj,
			read_indirect_object_with_visited(file_bytes, current, visited_refs));

		if (const auto next_ref = obj.value.as_ref()) {
			current = *next_ref;
			continue;
		}
		return std::move(obj.value);
	}
}

PdfResult<PdfValue> XrefTable::resolve_value(
	std::span<const uint8_t> file_bytes,
	const PdfValue& value) const noexcept
{
	if (const auto ref = value.as_ref()) {
		return resolve_object(file_bytes, *ref);
	}
	return value.deep_clone();
}

} // namespace recto::pdf
