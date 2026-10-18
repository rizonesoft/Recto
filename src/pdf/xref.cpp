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
						"Cumulative /Type /XRef entry count exceeds maximum budget");
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
					"/Type /XRef default /Index [0 /Size] exceeds maximum entry budget");
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
		std::unordered_map<uint32_t, std::pair<uint16_t, int64_t>> recovered_integers;
		std::unordered_map<uint32_t, uint16_t> catalog_generations;
		std::vector<IndirectRef> catalog_history;
		std::unordered_map<uint32_t, std::vector<uint32_t>> objstm_members;

		auto invalidate_objstm_container = [&](uint32_t container_id) {
			auto m_it = objstm_members.find(container_id);
			if (m_it == objstm_members.end()) {
				return;
			}
			for (const uint32_t member_id : m_it->second) {
				const XrefEntry* me = table.find(member_id);
				if (me != nullptr && me->is_compressed() && me->obj_stm_num == container_id) {
					table.entries_.erase(member_id);
					if (objects_discovered > 0u) {
						--objects_discovered;
					}
					recovered_integers.erase(member_id);
					catalog_generations.erase(member_id);
					if (info_candidate.has_value() && info_candidate->obj_num == member_id) {
						info_candidate = std::nullopt;
					}
				}
			}
			objstm_members.erase(m_it);
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
				const size_t tr_end = std::max(tr_lexer.position(), pos + 7u);
				cumulative_parse_bytes += static_cast<uint64_t>(tr_end - pos);
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
					pos = tr_end;
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
					   file_bytes[p] <= '9' &&
					   digits_n <= 10u) {
					obj_num_u64 = obj_num_u64 * 10ULL + static_cast<uint64_t>(file_bytes[p] - '0');
					++p;
					++digits_n;
				}
				if (digits_n >= 1u &&
					digits_n <= 10u &&
					obj_num_u64 > 0ULL &&
					obj_num_u64 <= std::numeric_limits<uint32_t>::max() &&
					p < file_bytes.size() &&
					Lexer::is_pdf_whitespace(file_bytes[p])) {
					while (p < file_bytes.size() && Lexer::is_pdf_whitespace(file_bytes[p])) {
						++p;
					}
					uint64_t gen_u64 = 0;
					size_t digits_g = 0;
					while (p < file_bytes.size() &&
						   file_bytes[p] >= '0' &&
						   file_bytes[p] <= '9' &&
						   digits_g <= 5u) {
						gen_u64 = gen_u64 * 10ULL + static_cast<uint64_t>(file_bytes[p] - '0');
						++p;
						++digits_g;
					}
					if (digits_g >= 1u &&
						digits_g <= 5u &&
						gen_u64 <= std::numeric_limits<uint16_t>::max() &&
						p < file_bytes.size() &&
						Lexer::is_pdf_whitespace(file_bytes[p])) {
						while (p < file_bytes.size() && Lexer::is_pdf_whitespace(file_bytes[p])) {
							++p;
						}
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
								[&recovered_integers, obj_num](IndirectRef len_ref) -> PdfResult<int64_t> {
									if (len_ref.obj_num == obj_num) {
										return make_pdf_error(
											ErrorCode::DamagedFormat,
											"Indirect /Length target cannot reference its own stream object");
									}
									const auto it = recovered_integers.find(len_ref.obj_num);
									if (it == recovered_integers.end() ||
										it->second.first != len_ref.gen_num) {
										return make_pdf_error(
											ErrorCode::DamagedFormat,
											"Indirect /Length target not available as an integer object during recovery");
									}
									return it->second.second;
								});
							const size_t next_pos = std::max(obj_parser.lexer().position(), p + 3u);
							cumulative_parse_bytes += static_cast<uint64_t>(next_pos - pos);
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
							recovered_integers.erase(obj_num);
							catalog_generations.erase(obj_num);
							if (info_candidate.has_value() &&
								info_candidate->obj_num == obj_num) {
								info_candidate = std::nullopt;
							}

							if (const auto int_val = parsed_obj->value.as_int()) {
								recovered_integers[obj_num] = {gen_num, *int_val};
							} else if (const PdfDict* dict_only = parsed_obj->value.as_dict()) {
								const auto type_opt = dict_only->get_name("Type");
								if (type_opt.has_value() && *type_opt == "Catalog") {
									catalog_generations[obj_num] = gen_num;
									catalog_history.push_back(IndirectRef{obj_num, gen_num});
								} else if (
									!type_opt.has_value() &&
									(dict_only->contains("Title") ||
									 dict_only->contains("Author") ||
									 dict_only->contains("Producer") ||
									 dict_only->contains("CreationDate"))) {
									info_candidate = IndirectRef{obj_num, gen_num};
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
								} else if (type_opt.has_value() && *type_opt == "ObjStm" && gen_num == 0u) {
									// Also unpack any intact generation-0 `/Type /ObjStm` stream discovered during
									// recovery so compressed objects inside object streams remain accessible and
									// override earlier revisions regardless of storage kind.
									const auto n_opt = sdict.get_int("N");
									const auto first_opt = sdict.get_int("First");
									auto dec_stm = decode_stream(stm);
									if (!dec_stm.has_value() &&
										(dec_stm.error().code == ErrorCode::OutOfMemory ||
										 dec_stm.error().code == ErrorCode::DecompressionLimitExceeded)) {
										return dec_stm.error();
									}
									if (dec_stm.has_value()) {
										cumulative_decoded_objstm_bytes += static_cast<uint64_t>(dec_stm->size());
										if (cumulative_decoded_objstm_bytes > max_recovery_decoded_objstm_bytes) {
											return make_xref_error(
												ErrorCode::DecompressionLimitExceeded,
												DiagnosticId::DamagedStream,
												"Damaged-file recovery exceeded cumulative decoded /ObjStm byte budget",
												pos);
										}
									}
									if (n_opt.has_value() &&
										*n_opt > 0 &&
										*n_opt <= static_cast<int64_t>(kMaxObjStmEntries) &&
										first_opt.has_value() &&
										*first_opt >= 0 &&
										dec_stm.has_value() &&
										static_cast<size_t>(*first_opt) <= dec_stm->size()) {
										const size_t first_off = static_cast<size_t>(*first_opt);
										const std::span<const uint8_t> hdr_span(
											dec_stm->data(),
											first_off);
										Lexer hdr_lex(hdr_span);
										struct InnerPair {
											uint32_t obj_num = 0;
											size_t rel_off = 0;
										};
										std::vector<InnerPair> inner_pairs;
										inner_pairs.reserve(static_cast<size_t>(*n_opt));
										bool header_ok = true;
										for (int64_t idx = 0; idx < *n_opt; ++idx) {
											Token id_tok;
											Token off_tok;
											PdfError herr;
											if (!next_non_comment_token(hdr_lex, id_tok, herr) ||
												!next_non_comment_token(hdr_lex, off_tok, herr)) {
												header_ok = false;
												break;
											}
											if (id_tok.kind != TokenKind::Integer ||
												id_tok.int_value <= 0 ||
												id_tok.int_value >
													static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) ||
												id_tok.int_value == static_cast<int64_t>(obj_num) ||
												off_tok.kind != TokenKind::Integer ||
												off_tok.int_value < 0) {
												header_ok = false;
												break;
											}
											const uint64_t rel_u64 = static_cast<uint64_t>(off_tok.int_value);
											if (rel_u64 > dec_stm->size() - first_off ||
												(!inner_pairs.empty() &&
												 static_cast<size_t>(rel_u64) < inner_pairs.back().rel_off)) {
												header_ok = false;
												break;
											}
											inner_pairs.push_back(InnerPair{
												static_cast<uint32_t>(id_tok.int_value),
												static_cast<size_t>(rel_u64)});
										}
										if (header_ok) {
											auto& member_vec = objstm_members[obj_num];
											for (size_t idx = 0; idx < inner_pairs.size(); ++idx) {
												if (candidate_attempts >= effective_max_entries) {
													return make_xref_error(
														ErrorCode::DamagedFormat,
														DiagnosticId::DamagedXRef,
														"Damaged-file recovery exceeded maximum object entry budget",
														pos);
												}
												++candidate_attempts;
												const uint32_t inner_id = inner_pairs[idx].obj_num;

												const size_t slice_start = first_off + inner_pairs[idx].rel_off;
												const size_t slice_end = (idx + 1u < inner_pairs.size())
													? (first_off + inner_pairs[idx + 1u].rel_off)
													: dec_stm->size();
												const std::span<const uint8_t> slice_span(
													dec_stm->data() + slice_start,
													slice_end - slice_start);
												Parser inner_parser(slice_span);
												auto inner_val = inner_parser.parse_direct_object();
												if (!inner_val.has_value()) {
													if (inner_val.error().code == ErrorCode::OutOfMemory) {
														return inner_val.error();
													}
													continue;
												}
												Token trailing_tok;
												PdfError terr;
												if (!next_non_comment_token(
														inner_parser.lexer(),
														trailing_tok,
														terr) ||
													!trailing_tok.is_eof()) {
													continue;
												}
												if (const PdfDict* idict_chk = inner_val->as_dict()) {
													if (const auto itype_chk = idict_chk->get_name("Type")) {
														if (*itype_chk == "ObjStm" || *itype_chk == "XRef") {
															continue;
														}
													}
												}

												if (!table.entries_.contains(inner_id) &&
													objects_discovered >= effective_max_entries) {
													return make_xref_error(
														ErrorCode::DamagedFormat,
														DiagnosticId::DamagedXRef,
														"Damaged-file recovery exceeded maximum object entry budget",
														pos);
												}
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
												recovered_integers.erase(inner_id);
												catalog_generations.erase(inner_id);
												if (info_candidate.has_value() &&
													info_candidate->obj_num == inner_id) {
													info_candidate = std::nullopt;
												}
												if (inner_id > max_obj_num) {
													max_obj_num = inner_id;
												}
												if (const auto int_v = inner_val->as_int()) {
													recovered_integers[inner_id] = {uint16_t{0}, *int_v};
												} else if (const PdfDict* idict = inner_val->as_dict()) {
													const auto itype = idict->get_name("Type");
													if (itype.has_value() && *itype == "Catalog") {
														catalog_generations[inner_id] = 0u;
														catalog_history.push_back(IndirectRef{inner_id, 0u});
													} else if (
														!itype.has_value() &&
														(idict->contains("Title") ||
														 idict->contains("Author") ||
														 idict->contains("Producer") ||
														 idict->contains("CreationDate"))) {
														info_candidate = IndirectRef{inner_id, 0u};
													}
												}
											}
										}
									}
								}
							}

							pos = next_pos;
							continue;
						}
					}
				}
			}

			++pos;
		}

		// Validate that a candidate `/Root` reference resolves with matching
		// generation to a dictionary whose `/Type` is `/Catalog` using the
		// single-pass `catalog_generations` map (zero secondary reparsing).
		auto is_valid_catalog_ref = [&catalog_generations](IndirectRef ref) noexcept -> bool {
			const auto it = catalog_generations.find(ref.obj_num);
			return it != catalog_generations.end() && it->second == ref.gen_num;
		};

		auto has_valid_root_entry = [&table, &is_valid_catalog_ref]() noexcept -> bool {
			const auto root_opt = table.trailer_.get_ref("Root");
			return root_opt.has_value() && is_valid_catalog_ref(*root_opt);
		};

		if (!has_valid_root_entry()) {
			for (auto it = catalog_history.rbegin(); it != catalog_history.rend(); ++it) {
				if (is_valid_catalog_ref(*it)) {
					table.trailer_.set("Root", PdfValue(*it));
					break;
				}
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
								return try_recover(make_xref_error(
									ErrorCode::DamagedFormat,
									DiagnosticId::DamagedXRef,
									"In-use xref entry offset points outside file bounds",
									off_tok.byte_offset,
									obj_num,
									gen_num));
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

	auto cache_it = obj_stm_cache_.find(stm_obj_num);
	if (cache_it == obj_stm_cache_.end()) {
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
		const auto type_name = obj_stm.dict.get_name("Type");
		if (!type_name.has_value() || *type_name != "ObjStm") {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"Compressed object container stream is missing /Type /ObjStm",
				stm_entry->byte_offset,
				stm_obj_num,
				0u);
		}

		const auto n_opt = obj_stm.dict.get_int("N");
		const auto first_opt = obj_stm.dict.get_int("First");
		if (!n_opt.has_value() || *n_opt <= 0 ||
			*n_opt > static_cast<int64_t>(kMaxObjStmEntries) ||
			!first_opt.has_value() || *first_opt < 0) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"Invalid or missing /N or /First in /Type /ObjStm dictionary",
				stm_entry->byte_offset,
				stm_obj_num,
				0u);
		}

		auto decoded_res = decode_stream(obj_stm);
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

		const uint64_t first_off = static_cast<uint64_t>(*first_opt);
		if (first_off > decoded_res->size()) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"/Type /ObjStm /First offset exceeds decoded stream length",
				stm_entry->byte_offset,
				stm_obj_num,
				0u);
		}

		const uint32_t n_count = static_cast<uint32_t>(*n_opt);
		CachedObjStream cached;
		cached.first_offset = first_off;
		cached.entries.reserve(n_count);

		// Parse the `/N` pairs of `<obj_num> <relative_offset>` exclusively from
		// `[0, first_off)` so a malformed header cannot bleed into object bodies.
		const std::span<const uint8_t> header_bytes(
			decoded_res->data(),
			static_cast<size_t>(first_off));
		Lexer hdr_lexer(header_bytes);
		for (uint32_t i = 0; i < n_count; ++i) {
			Token num_tok;
			Token off_tok;
			PdfError herr;
			if (!next_non_comment_token(hdr_lexer, num_tok, herr) ||
				!next_non_comment_token(hdr_lexer, off_tok, herr)) {
				return make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedObject,
					"Truncated or malformed header table in /Type /ObjStm stream",
					stm_entry->byte_offset,
					stm_obj_num,
					0u);
			}
			if (num_tok.kind != TokenKind::Integer ||
				num_tok.int_value <= 0 ||
				num_tok.int_value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) ||
				off_tok.kind != TokenKind::Integer ||
				off_tok.int_value < 0) {
				return make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedObject,
					"Invalid object number or relative offset in /Type /ObjStm header",
					stm_entry->byte_offset,
					stm_obj_num,
					0u);
			}
			const uint64_t rel_off = static_cast<uint64_t>(off_tok.int_value);
			if (rel_off > decoded_res->size() - first_off ||
				(!cached.entries.empty() && rel_off < cached.entries.back().rel_offset)) {
				return make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedObject,
					"/Type /ObjStm entry relative offset is out of bounds or non-monotonic",
					stm_entry->byte_offset,
					stm_obj_num,
					0u);
			}
			cached.entries.push_back(CachedObjStream::HeaderPair{
				static_cast<uint32_t>(num_tok.int_value),
				rel_off});
		}

		cached.decoded_bytes = std::move(*decoded_res);
		if (obj_stm_cache_.size() >= kMaxCachedObjStreams) {
			obj_stm_cache_.erase(obj_stm_cache_.begin());
		}
		cache_it = obj_stm_cache_.emplace(stm_obj_num, std::move(cached)).first;
	}

	const CachedObjStream& cached_stm = cache_it->second;
	if (stm_index >= cached_stm.entries.size()) {
		return make_xref_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedObject,
			"Compressed object index exceeds /Type /ObjStm /N entry count",
			std::nullopt,
			ref.obj_num,
			ref.gen_num);
	}

	const CachedObjStream::HeaderPair& pair = cached_stm.entries[stm_index];
	if (pair.obj_num != ref.obj_num) {
		return make_xref_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedObject,
			"Compressed object number in /Type /ObjStm header does not match xref entry",
			std::nullopt,
			ref.obj_num,
			ref.gen_num);
	}

	const size_t obj_start = static_cast<size_t>(cached_stm.first_offset + pair.rel_offset);
	size_t obj_end = cached_stm.decoded_bytes.size();
	if (stm_index + 1u < cached_stm.entries.size()) {
		obj_end = static_cast<size_t>(
			cached_stm.first_offset + cached_stm.entries[stm_index + 1u].rel_offset);
	}

	const std::span<const uint8_t> obj_slice(
		cached_stm.decoded_bytes.data() + obj_start,
		obj_end - obj_start);
	Parser inner_parser(obj_slice);
	auto val_res = inner_parser.parse_direct_object();
	if (!val_res.has_value()) {
		if (val_res.error().code == ErrorCode::DamagedFormat) {
			record_diagnostic(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				std::nullopt,
				ref.obj_num,
				ref.gen_num);
		}
		return val_res.error();
	}

	// Per ISO 32000-1 section 7.5.7, each slice `[obj_start, obj_end)` in an
	// `/ObjStm` contains a single direct PDF object with no `N G obj` header,
	// `stream` payload, `endobj` footer, or trailing tokens.
	Token trailing_tok;
	PdfError terr;
	if (!next_non_comment_token(inner_parser.lexer(), trailing_tok, terr)) {
		if (terr.code == ErrorCode::DamagedFormat) {
			record_diagnostic(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				std::nullopt,
				ref.obj_num,
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

	return IndirectObject{ref, std::move(*val_res)};
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
