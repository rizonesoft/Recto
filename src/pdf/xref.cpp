// Classic PDF cross-reference table parser, trailer merger, incremental /Prev
// chain follower, and cycle-guarded indirect object resolver (ISO 32000-1
// sections 7.5.4 and 7.5.6; D01 T01 section 5).
#include "xref.h"

#include <algorithm>
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

PdfResult<XrefTable> XrefTable::parse(std::span<const uint8_t> file_bytes) noexcept
{
	try {
		std::string version_str;
		RECTO_PDF_TRY(const uint64_t header_off, locate_header_offset(file_bytes, &version_str));
		RECTO_PDF_TRY(const uint64_t raw_startxref, locate_startxref_offset(file_bytes));

		if (raw_startxref > file_bytes.size() - header_off ||
			raw_startxref + header_off >= file_bytes.size()) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedXRef,
				"startxref byte offset points outside file bounds",
				raw_startxref);
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
				return err;
			}
			if (!kw_tok.is_keyword("xref")) {
				return make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedXRef,
					"Expected 'xref' keyword at cross-reference table offset",
					current_xref_offset);
			}

			// Parse one or more `<first_id> <count>` subsections until `trailer`.
			while (true) {
				Token first_tok;
				if (!next_non_comment_token(lexer, first_tok, err)) {
					return err;
				}
				if (first_tok.is_keyword("trailer")) {
					break;
				}
				if (first_tok.kind != TokenKind::Integer ||
					first_tok.int_value < 0 ||
					first_tok.int_value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
					return make_xref_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedXRef,
						"Invalid first object number in xref subsection header",
						first_tok.byte_offset);
				}

				Token count_tok;
				if (!next_non_comment_token(lexer, count_tok, err)) {
					return err;
				}
				if (count_tok.kind != TokenKind::Integer ||
					count_tok.int_value < 0 ||
					count_tok.int_value > static_cast<int64_t>(kMaxXrefSubsectionCount)) {
					return make_xref_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedXRef,
						"Invalid or excessive object count in xref subsection header",
						count_tok.byte_offset);
				}

				const uint64_t first_id = static_cast<uint64_t>(first_tok.int_value);
				const uint64_t count = static_cast<uint64_t>(count_tok.int_value);
				if (first_id + count >
					static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1ULL) {
					return make_xref_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedXRef,
						"xref subsection object range overflows 32-bit object numbers",
						first_tok.byte_offset);
				}
				if (count > kMaxXrefTotalEntries - total_rows_parsed) {
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
						return err;
					}

					if (off_tok.kind != TokenKind::Integer || off_tok.int_value < 0) {
						return make_xref_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedXRef,
							"Invalid byte offset in classic xref entry",
							off_tok.byte_offset);
					}
					if (gen_tok.kind != TokenKind::Integer ||
						gen_tok.int_value < 0 ||
						gen_tok.int_value > static_cast<int64_t>(std::numeric_limits<uint16_t>::max())) {
						return make_xref_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedXRef,
							"Invalid generation number in classic xref entry",
							gen_tok.byte_offset);
					}
					if (flag_tok.kind != TokenKind::Keyword ||
						(flag_tok.text != "n" && flag_tok.text != "f")) {
						return make_xref_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedXRef,
							"Expected 'n' or 'f' status flag in classic xref entry",
							flag_tok.byte_offset);
					}

					const uint32_t obj_num = static_cast<uint32_t>(first_id + i);
					const uint16_t gen_num = static_cast<uint16_t>(gen_tok.int_value);
					const uint64_t raw_off = static_cast<uint64_t>(off_tok.int_value);

					if (flag_tok.text == "n") {
						if (obj_num == 0) {
							// Object 0 is always free per ISO 32000-1 section 7.5.4;
							// tolerate writers that mark object 0 'n' by treating it as free.
							table.entries_.try_emplace(
								0u,
								XrefEntry{XrefEntryKind::Free, raw_off, 0, 0, gen_num});
							continue;
						}
						if (raw_off > file_bytes.size() - header_off ||
							raw_off + header_off >= file_bytes.size()) {
							return make_xref_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedXRef,
								"In-use xref entry offset points outside file bounds",
								off_tok.byte_offset,
								obj_num,
								gen_num);
						}
						table.entries_.try_emplace(
							obj_num,
							XrefEntry{
								XrefEntryKind::Uncompressed,
								raw_off + header_off,
								0,
								0,
								gen_num});
					} else {
						table.entries_.try_emplace(
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

			// Parse the `trailer << ... >>` dictionary immediately following `trailer`.
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
				return tr_err;
			}
			const PdfDict& rev_trailer = *trailer_res;
			auto merge_res = merge_trailer_from_revision(table.trailer_, rev_trailer);
			if (!merge_res.has_value()) {
				return merge_res.error();
			}

			// Guard hybrid `/XRefStm` offsets against cycles and out-of-bounds pointers,
			// and reject hybrid XRef streams with UnsupportedFeature until §6.
			if (const PdfValue* xref_stm_val = rev_trailer.get("XRefStm")) {
				const auto stm_off_opt = xref_stm_val->as_int();
				if (!stm_off_opt.has_value() || *stm_off_opt < 0) {
					return make_xref_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedTrailer,
						"Invalid /XRefStm offset in trailer dictionary",
						current_xref_offset);
				}
				const uint64_t raw_stm_off = static_cast<uint64_t>(*stm_off_opt);
				if (raw_stm_off > file_bytes.size() - header_off ||
					raw_stm_off + header_off >= file_bytes.size()) {
					return make_xref_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedXRef,
						"/XRefStm offset points outside file bounds",
						current_xref_offset);
				}
				const uint64_t adjusted_stm_off = raw_stm_off + header_off;
				if (!visited_offsets.insert(adjusted_stm_off).second) {
					return make_xref_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedXRef,
						"Cyclic /XRefStm cross-reference offset detected",
						adjusted_stm_off);
				}
				return make_pdf_error(
					ErrorCode::UnsupportedFeature,
					"Hybrid /XRefStm cross-reference stream requires PDF 1.5+ XRef stream parser",
					adjusted_stm_off);
			}

			// Follow `/Prev` incremental update chain if present.
			const PdfValue* prev_val = rev_trailer.get("Prev");
			if (prev_val == nullptr) {
				break;
			}
			const auto prev_int = prev_val->as_int();
			if (!prev_int.has_value() || *prev_int < 0) {
				return make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedTrailer,
					"Invalid /Prev offset in trailer dictionary",
					current_xref_offset);
			}
			const uint64_t raw_prev = static_cast<uint64_t>(*prev_int);
			if (raw_prev > file_bytes.size() - header_off ||
				raw_prev + header_off >= file_bytes.size()) {
				return make_xref_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedXRef,
					"/Prev cross-reference offset points outside file bounds",
					current_xref_offset);
			}
			current_xref_offset = raw_prev + header_off;
		}

		if (!table.trailer_.contains("Root")) {
			return make_xref_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedTrailer,
				"Trailer dictionary is missing required /Root catalog reference",
				raw_startxref + header_off);
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
	if (entry->generation != ref.gen_num) {
		return IndirectObject{ref, PdfValue(PdfNull{})};
	}
	if (!entry->is_uncompressed()) {
		return make_pdf_error(
			ErrorCode::UnsupportedFeature,
			"Compressed /ObjStm entry resolution requires object stream decoder",
			entry->byte_offset,
			ref.obj_num,
			ref.gen_num);
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
