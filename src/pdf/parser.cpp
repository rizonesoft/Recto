// Bounded recursive-descent PDF 1.7 and 2.0 object and stream parser
// (ISO 32000-1:2008 section 7.3, ISO 32000-2:2020 section 7.3) for recto-pdf
// (D01 T01 section 4).
#include "parser.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

namespace recto::pdf {
namespace {

constexpr std::string_view kEndstreamKeyword = "endstream";
constexpr std::string_view kEndobjKeyword = "endobj";

[[nodiscard]] bool matches_keyword_at(
	std::span<const uint8_t> input,
	size_t pos,
	std::string_view kw) noexcept
{
	if (pos + kw.size() > input.size()) {
		return false;
	}
	if (std::memcmp(input.data() + pos, kw.data(), kw.size()) != 0) {
		return false;
	}
	const size_t after = pos + kw.size();
	return after == input.size() || !Lexer::is_pdf_regular(input[after]);
}

[[nodiscard]] bool is_endstream_followed_by_terminator(
	std::span<const uint8_t> input,
	size_t endstream_pos,
	bool require_endobj_terminator) noexcept
{
	size_t cur = endstream_pos + kEndstreamKeyword.size();
	while (cur < input.size()) {
		if (Lexer::is_pdf_whitespace(input[cur])) {
			++cur;
			continue;
		}
		if (input[cur] == '%') {
			++cur;
			while (cur < input.size() && input[cur] != '\n' && input[cur] != '\r') {
				++cur;
			}
			continue;
		}
		break;
	}
	if (matches_keyword_at(input, cur, kEndobjKeyword)) {
		return true;
	}
	return !require_endobj_terminator && cur == input.size();
}

[[nodiscard]] bool next_non_comment(
	Lexer& lexer,
	Token& out_tok,
	PdfError& err)
{
	while (true) {
		auto res = lexer.next_token();
		if (!res.has_value()) {
			err = std::move(res).error();
			return false;
		}
		if (res.value().kind == TokenKind::Comment) {
			continue;
		}
		out_tok = std::move(res).value();
		return true;
	}
}

[[nodiscard]] bool peek_first_non_comment(
	Lexer& lexer,
	Token& out_tok,
	PdfError& err)
{
	while (true) {
		auto res = lexer.peek_token(0);
		if (!res.has_value()) {
			err = std::move(res).error();
			return false;
		}
		if (res.value().kind == TokenKind::Comment) {
			auto disc = lexer.next_token();
			if (!disc.has_value()) {
				err = std::move(disc).error();
				return false;
			}
			continue;
		}
		out_tok = std::move(res).value();
		return true;
	}
}

[[nodiscard]] bool parse_integer_or_ref(
	Lexer& lexer,
	const Token& tok0,
	PdfValue& out,
	PdfError& err)
{
	Token p1;
	PdfError peek_err;
	if (!peek_first_non_comment(lexer, p1, peek_err)) {
		if (peek_err.code == ErrorCode::OutOfMemory ||
			peek_err.code == ErrorCode::InternalError) {
			err = std::move(peek_err);
			return false;
		}
		out = PdfValue(tok0.int_value);
		return true;
	}

	if (p1.kind != TokenKind::Integer) {
		out = PdfValue(tok0.int_value);
		return true;
	}

	int64_t gen_int = 0;
	bool is_ref = false;

	if (!lexer.emit_comments()) {
		auto p2 = lexer.peek_token(1);
		if (!p2.has_value()) {
			if (p2.error().code == ErrorCode::OutOfMemory ||
				p2.error().code == ErrorCode::InternalError) {
				err = std::move(p2).error();
				return false;
			}
			out = PdfValue(tok0.int_value);
			return true;
		}
		if (p2.value().is_keyword("R")) {
			Token gen_tok;
			Token r_tok;
			if (!next_non_comment(lexer, gen_tok, err) ||
				!next_non_comment(lexer, r_tok, err)) {
				return false;
			}
			gen_int = gen_tok.int_value;
			is_ref = true;
		}
	} else {
		const size_t pos_before_p1 = lexer.position();
		Token gen_tok;
		if (!next_non_comment(lexer, gen_tok, err)) {
			return false;
		}
		Token p2;
		if (!peek_first_non_comment(lexer, p2, peek_err)) {
			if (peek_err.code == ErrorCode::OutOfMemory ||
				peek_err.code == ErrorCode::InternalError) {
				err = std::move(peek_err);
				return false;
			}
			lexer.seek(pos_before_p1);
			out = PdfValue(tok0.int_value);
			return true;
		}
		if (p2.is_keyword("R")) {
			Token r_tok;
			if (!next_non_comment(lexer, r_tok, err)) {
				return false;
			}
			gen_int = gen_tok.int_value;
			is_ref = true;
		} else {
			lexer.seek(pos_before_p1);
		}
	}

	if (!is_ref) {
		out = PdfValue(tok0.int_value);
		return true;
	}

	if (tok0.int_value <= 0 ||
		tok0.int_value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) ||
		gen_int < 0 ||
		gen_int > static_cast<int64_t>(std::numeric_limits<uint16_t>::max())) {
		err = make_pdf_error(
			ErrorCode::DamagedFormat,
			"Invalid object or generation number in indirect reference",
			tok0.byte_offset);
		return false;
	}

	out = PdfValue(IndirectRef{
		static_cast<uint32_t>(tok0.int_value),
		static_cast<uint16_t>(gen_int)});
	return true;
}

} // namespace

Parser::Parser(
	Lexer& lexer,
	size_t max_recursion_depth,
	size_t max_container_elements) noexcept
	: owned_lexer_(std::nullopt)
	, lexer_(&lexer)
	, max_recursion_depth_(std::min(max_recursion_depth, kMaxParserRecursionDepth))
	, max_container_elements_(std::min(max_container_elements, kMaxContainerElements))
{
}

Parser::Parser(
	std::span<const uint8_t> input,
	size_t max_recursion_depth,
	size_t max_container_elements) noexcept
	: owned_lexer_(std::in_place, input)
	, lexer_(&owned_lexer_.value())
	, max_recursion_depth_(std::min(max_recursion_depth, kMaxParserRecursionDepth))
	, max_container_elements_(std::min(max_container_elements, kMaxContainerElements))
{
}

void Parser::set_max_recursion_depth(size_t max_depth) noexcept
{
	max_recursion_depth_ = std::min(max_depth, kMaxParserRecursionDepth);
}

void Parser::set_max_container_elements(size_t max_elements) noexcept
{
	max_container_elements_ = std::min(max_elements, kMaxContainerElements);
}

PdfResult<PdfValue> Parser::parse_direct_object() noexcept
{
	const size_t saved_pos = lexer_->position();
	try {
		PdfValue out;
		PdfError err;
		if (!parse_direct_object_impl(0, out, err)) {
			if (err.code == ErrorCode::OutOfMemory) {
				lexer_->seek(saved_pos);
			}
			return err;
		}
		return out;
	} catch (const std::bad_alloc&) {
		lexer_->seek(saved_pos);
		return PdfError{
			ErrorCode::OutOfMemory,
			"Out of memory in Parser::parse_direct_object",
			saved_pos};
	} catch (...) {
		lexer_->seek(saved_pos);
		return PdfError{
			ErrorCode::InternalError,
			"Unexpected failure in Parser::parse_direct_object",
			saved_pos};
	}
}

PdfResult<PdfArray> Parser::parse_array() noexcept
{
	const size_t saved_pos = lexer_->position();
	try {
		Token open_tok;
		PdfError err;
		if (!next_non_comment(*lexer_, open_tok, err)) {
			if (err.code == ErrorCode::OutOfMemory) {
				lexer_->seek(saved_pos);
			}
			return err;
		}
		if (open_tok.kind != TokenKind::ArrayOpen) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"Expected '[' at start of PDF array",
				open_tok.byte_offset);
		}
		PdfArray arr;
		if (!parse_array_body(0, open_tok.byte_offset, arr, err)) {
			if (err.code == ErrorCode::OutOfMemory) {
				lexer_->seek(saved_pos);
			}
			return err;
		}
		return arr;
	} catch (const std::bad_alloc&) {
		lexer_->seek(saved_pos);
		return PdfError{
			ErrorCode::OutOfMemory,
			"Out of memory in Parser::parse_array",
			saved_pos};
	} catch (...) {
		lexer_->seek(saved_pos);
		return PdfError{
			ErrorCode::InternalError,
			"Unexpected failure in Parser::parse_array",
			saved_pos};
	}
}

PdfResult<PdfDict> Parser::parse_dictionary() noexcept
{
	const size_t saved_pos = lexer_->position();
	try {
		Token open_tok;
		PdfError err;
		if (!next_non_comment(*lexer_, open_tok, err)) {
			if (err.code == ErrorCode::OutOfMemory) {
				lexer_->seek(saved_pos);
			}
			return err;
		}
		if (open_tok.kind != TokenKind::DictOpen) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"Expected '<<' at start of PDF dictionary",
				open_tok.byte_offset);
		}
		PdfDict dict;
		if (!parse_dictionary_body(0, open_tok.byte_offset, dict, err)) {
			if (err.code == ErrorCode::OutOfMemory) {
				lexer_->seek(saved_pos);
			}
			return err;
		}
		return dict;
	} catch (const std::bad_alloc&) {
		lexer_->seek(saved_pos);
		return PdfError{
			ErrorCode::OutOfMemory,
			"Out of memory in Parser::parse_dictionary",
			saved_pos};
	} catch (...) {
		lexer_->seek(saved_pos);
		return PdfError{
			ErrorCode::InternalError,
			"Unexpected failure in Parser::parse_dictionary",
			saved_pos};
	}
}

PdfResult<std::span<const uint8_t>> Parser::parse_stream_slice(
	const PdfDict& stream_dict,
	const LengthResolver& length_resolver) noexcept
{
	const size_t saved_pos = lexer_->position();
	try {
		auto res = parse_stream_slice_impl(
			stream_dict,
			length_resolver,
			/*require_endobj_terminator=*/false);
		if (!res.has_value() && res.error().code == ErrorCode::OutOfMemory) {
			lexer_->seek(saved_pos);
		}
		return res;
	} catch (const std::bad_alloc&) {
		lexer_->seek(saved_pos);
		return PdfError{
			ErrorCode::OutOfMemory,
			"Out of memory in Parser::parse_stream_slice",
			saved_pos};
	} catch (...) {
		lexer_->seek(saved_pos);
		return PdfError{
			ErrorCode::InternalError,
			"Unexpected failure in Parser::parse_stream_slice",
			saved_pos};
	}
}

PdfResult<IndirectObject> Parser::parse_indirect_object(
	const LengthResolver& length_resolver) noexcept
{
	const size_t saved_pos = lexer_->position();
	try {
		auto res = parse_indirect_object_impl(length_resolver);
		if (!res.has_value() && res.error().code == ErrorCode::OutOfMemory) {
			lexer_->seek(saved_pos);
		}
		return res;
	} catch (const std::bad_alloc&) {
		lexer_->seek(saved_pos);
		return PdfError{
			ErrorCode::OutOfMemory,
			"Out of memory in Parser::parse_indirect_object",
			saved_pos};
	} catch (...) {
		lexer_->seek(saved_pos);
		return PdfError{
			ErrorCode::InternalError,
			"Unexpected failure in Parser::parse_indirect_object",
			saved_pos};
	}
}

bool Parser::parse_direct_object_impl(
	size_t depth,
	PdfValue& out,
	PdfError& err)
{
	Token tok0;
	if (!next_non_comment(*lexer_, tok0, err)) {
		return false;
	}

	switch (tok0.kind) {
	case TokenKind::Eof:
		err = make_pdf_error(
			ErrorCode::DamagedFormat,
			"Unexpected EOF while parsing PDF object",
			tok0.byte_offset);
		return false;

	case TokenKind::Boolean:
		out = PdfValue(tok0.bool_value);
		return true;

	case TokenKind::Null:
		out = PdfValue(PdfNull{});
		return true;

	case TokenKind::Real:
		out = PdfValue(tok0.real_value);
		return true;

	case TokenKind::Integer:
		return parse_integer_or_ref(*lexer_, tok0, out, err);

	case TokenKind::Name:
		out = PdfValue(PdfName::from_decoded(tok0.text.view()));
		return true;

	case TokenKind::LiteralString:
		out = PdfValue(PdfString(tok0.bytes.span(), false));
		return true;

	case TokenKind::HexString:
		out = PdfValue(PdfString(tok0.bytes.span(), true));
		return true;

	case TokenKind::ArrayOpen:
		out = PdfValue(PdfArray{});
		return parse_array_body(depth, tok0.byte_offset, *out.as_array_mut(), err);

	case TokenKind::DictOpen:
		out = PdfValue(PdfDict{});
		return parse_dictionary_body(depth, tok0.byte_offset, *out.as_dict_mut(), err);

	case TokenKind::ArrayClose:
	case TokenKind::DictClose:
	case TokenKind::BraceOpen:
	case TokenKind::BraceClose:
	case TokenKind::Keyword:
	case TokenKind::Comment:
		err = make_pdf_error(
			ErrorCode::DamagedFormat,
			"Unexpected token while parsing PDF direct object",
			tok0.byte_offset);
		return false;
	}

	err = make_pdf_error(
		ErrorCode::DamagedFormat,
		"Unrecognized token kind in PDF parser",
		tok0.byte_offset);
	return false;
}

bool Parser::parse_array_body(
	size_t depth,
	uint64_t open_offset,
	PdfArray& out,
	PdfError& err)
{
	if (depth >= max_recursion_depth_) {
		err = make_pdf_error(
			ErrorCode::RecursionLimitExceeded,
			"PDF array nesting depth exceeds maximum recursion limit",
			open_offset);
		return false;
	}

	Token tok;
	while (true) {
		if (!peek_first_non_comment(*lexer_, tok, err)) {
			return false;
		}
		if (tok.kind == TokenKind::ArrayClose) {
			return next_non_comment(*lexer_, tok, err);
		}
		if (tok.is_eof()) {
			err = make_pdf_error(
				ErrorCode::DamagedFormat,
				"Unterminated PDF array: unexpected EOF before ']'",
				open_offset);
			return false;
		}
		if (out.size() >= max_container_elements_) {
			err = make_pdf_error(
				ErrorCode::DamagedFormat,
				"PDF array exceeds maximum element count ceiling",
				tok.byte_offset);
			return false;
		}
		out.emplace_back();
		if (!parse_direct_object_impl(depth + 1, out.back(), err)) {
			return false;
		}
	}
}

bool Parser::parse_dictionary_body(
	size_t depth,
	uint64_t open_offset,
	PdfDict& out,
	PdfError& err)
{
	if (depth >= max_recursion_depth_) {
		err = make_pdf_error(
			ErrorCode::RecursionLimitExceeded,
			"PDF dictionary nesting depth exceeds maximum recursion limit",
			open_offset);
		return false;
	}

	Token tok;
	PdfValue val;
	size_t pair_count = 0;
	while (true) {
		if (!peek_first_non_comment(*lexer_, tok, err)) {
			return false;
		}
		if (tok.kind == TokenKind::DictClose) {
			return next_non_comment(*lexer_, tok, err);
		}
		if (tok.is_eof()) {
			err = make_pdf_error(
				ErrorCode::DamagedFormat,
				"Unterminated PDF dictionary: unexpected EOF before '>>'",
				open_offset);
			return false;
		}
		if (pair_count >= max_container_elements_) {
			err = make_pdf_error(
				ErrorCode::DamagedFormat,
				"PDF dictionary exceeds maximum entry count ceiling",
				tok.byte_offset);
			return false;
		}
		if (!next_non_comment(*lexer_, tok, err)) {
			return false;
		}
		if (tok.kind != TokenKind::Name) {
			err = make_pdf_error(
				ErrorCode::DamagedFormat,
				"Expected PDF name key in dictionary",
				tok.byte_offset);
			return false;
		}
		PdfName key = PdfName::from_decoded(tok.text.view());
		val = PdfValue(PdfNull{});
		if (!parse_direct_object_impl(depth + 1, val, err)) {
			return false;
		}
		++pair_count;
		// PdfDict::set automatically omits or erases entries whose value is
		// PdfNull per ISO 32000-1 section 7.3.7.
		out.set(std::move(key), std::move(val));
	}
}

PdfResult<std::span<const uint8_t>> Parser::parse_stream_slice_impl(
	const PdfDict& stream_dict,
	const LengthResolver& length_resolver,
	bool require_endobj_terminator)
{
	const std::span<const uint8_t> input = lexer_->input();

	Token kw_tok;
	PdfError err;
	if (!next_non_comment(*lexer_, kw_tok, err)) {
		return err;
	}
	if (!kw_tok.is_keyword("stream")) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"Expected 'stream' keyword before stream payload",
			kw_tok.byte_offset);
	}

	RECTO_PDF_TRY(uint64_t stream_start_u64, lexer_->consume_stream_eol());
	const size_t stream_start = static_cast<size_t>(stream_start_u64);

	std::optional<int64_t> declared_length = std::nullopt;
	if (const PdfValue* len_val = stream_dict.get("Length")) {
		if (const auto direct_len = len_val->as_int()) {
			declared_length = *direct_len;
		} else if (const auto ref_len = len_val->as_ref()) {
			if (length_resolver) {
				auto resolved = length_resolver(*ref_len);
				if (!resolved.has_value()) {
					if (resolved.error().code == ErrorCode::OutOfMemory ||
						resolved.error().code == ErrorCode::InternalError) {
						return std::move(resolved).error();
					}
				} else {
					declared_length = resolved.value();
				}
			}
		}
	}

	const size_t avail = input.size() - stream_start;
	std::optional<size_t> declared_kw_pos = std::nullopt;
	size_t declared_len_size = 0;
	if (declared_length.has_value() && *declared_length >= 0) {
		const uint64_t len_u64 = static_cast<uint64_t>(*declared_length);
		if (len_u64 <= avail) {
			const size_t candidate_end = stream_start + static_cast<size_t>(len_u64);
			size_t kw_pos = candidate_end;
			// ISO 32000-1 section 7.3.8.1 allows an optional single EOL marker
			// (\r\n, \n, or \r) after the stream data and before `endstream`
			// (with optional horizontal space). Do not skip arbitrary NUL or
			// multi-line whitespace that could mask a wrong /Length.
			while (kw_pos < input.size() &&
				(input[kw_pos] == ' ' || input[kw_pos] == '\t')) {
				++kw_pos;
			}
			if (kw_pos < input.size() && input[kw_pos] == '\r') {
				++kw_pos;
				if (kw_pos < input.size() && input[kw_pos] == '\n') {
					++kw_pos;
				}
			} else if (kw_pos < input.size() && input[kw_pos] == '\n') {
				++kw_pos;
			}
			while (kw_pos < input.size() &&
				(input[kw_pos] == ' ' || input[kw_pos] == '\t')) {
				++kw_pos;
			}
			if (matches_keyword_at(input, kw_pos, kEndstreamKeyword)) {
				if (is_endstream_followed_by_terminator(
						input,
						kw_pos,
						require_endobj_terminator)) {
					lexer_->seek(kw_pos + kEndstreamKeyword.size());
					return input.subspan(stream_start, static_cast<size_t>(len_u64));
				}
				if (!require_endobj_terminator) {
					declared_kw_pos = kw_pos;
					declared_len_size = static_cast<size_t>(len_u64);
				}
			}
		}
	}

	// Fallback scan for `endstream` when /Length is missing, negative,
	// unresolved, out of bounds, or does not land on a valid `endstream`
	// terminator.
	std::optional<size_t> strict_match = std::nullopt;
	std::optional<size_t> boundary_match = std::nullopt;
	std::optional<size_t> any_match = std::nullopt;
	if (input.size() >= stream_start + kEndstreamKeyword.size()) {
		const size_t last_pos = input.size() - kEndstreamKeyword.size();
		for (size_t pos = stream_start; pos <= last_pos; ++pos) {
			if (input[pos] != 'e') {
				continue;
			}
			if (!matches_keyword_at(input, pos, kEndstreamKeyword)) {
				continue;
			}
			if (!any_match.has_value()) {
				any_match = pos;
			}
			const bool at_boundary =
				(pos == stream_start || !Lexer::is_pdf_regular(input[pos - 1]));
			if (at_boundary && !boundary_match.has_value()) {
				boundary_match = pos;
			}
			if (at_boundary &&
				is_endstream_followed_by_terminator(
					input,
					pos,
					require_endobj_terminator)) {
				strict_match = pos;
				break;
			}
		}
	}

	if (!strict_match.has_value() && declared_kw_pos.has_value()) {
		lexer_->seek(*declared_kw_pos + kEndstreamKeyword.size());
		return input.subspan(stream_start, declared_len_size);
	}

	const std::optional<size_t> match_pos = strict_match.has_value()
		? strict_match
		: (boundary_match.has_value() ? boundary_match : any_match);
	if (!match_pos.has_value()) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"Unterminated PDF stream: missing 'endstream' keyword",
			stream_start);
	}

	size_t payload_end = *match_pos;
	if (payload_end >= stream_start + 2 &&
		input[payload_end - 2] == '\r' &&
		input[payload_end - 1] == '\n') {
		payload_end -= 2;
	} else if (
		payload_end >= stream_start + 1 &&
		(input[payload_end - 1] == '\n' || input[payload_end - 1] == '\r')) {
		payload_end -= 1;
	}

	record_diagnostic(
		ErrorCode::DamagedFormat,
		DiagnosticId::DamagedStream,
		stream_start);
	lexer_->seek(*match_pos + kEndstreamKeyword.size());
	return input.subspan(stream_start, payload_end - stream_start);
}

PdfResult<IndirectObject> Parser::parse_indirect_object_impl(
	const LengthResolver& length_resolver)
{
	Token tok;
	PdfError err;
	if (!next_non_comment(*lexer_, tok, err)) {
		return err;
	}
	if (tok.kind != TokenKind::Integer ||
		tok.int_value <= 0 ||
		tok.int_value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"Invalid indirect object number: expected positive 32-bit integer",
			tok.byte_offset);
	}
	const uint32_t obj_num = static_cast<uint32_t>(tok.int_value);

	if (!next_non_comment(*lexer_, tok, err)) {
		return err;
	}
	if (tok.kind != TokenKind::Integer ||
		tok.int_value < 0 ||
		tok.int_value > static_cast<int64_t>(std::numeric_limits<uint16_t>::max())) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"Invalid indirect object generation number: expected 0..65535",
			tok.byte_offset);
	}
	const uint16_t gen_num = static_cast<uint16_t>(tok.int_value);

	if (!next_non_comment(*lexer_, tok, err)) {
		return err;
	}
	if (!tok.is_keyword("obj")) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"Expected 'obj' keyword in indirect object definition",
			tok.byte_offset);
	}

	PdfValue body;
	if (!parse_direct_object_impl(0, body, err)) {
		if (!err.object_num.has_value()) {
			err.object_num = obj_num;
			err.generation_num = gen_num;
		}
		return err;
	}

	if (!peek_first_non_comment(*lexer_, tok, err)) {
		if (!err.object_num.has_value()) {
			err.object_num = obj_num;
			err.generation_num = gen_num;
		}
		return err;
	}
	if (tok.is_keyword("stream")) {
		if (!body.is_dict()) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"PDF 'stream' keyword must be preceded by a stream dictionary",
				tok.byte_offset,
				obj_num,
				gen_num);
		}
		auto slice_res = parse_stream_slice_impl(
			*body.as_dict(),
			length_resolver,
			/*require_endobj_terminator=*/true);
		if (!slice_res.has_value()) {
			PdfError slice_err = std::move(slice_res).error();
			if (!slice_err.object_num.has_value()) {
				slice_err.object_num = obj_num;
				slice_err.generation_num = gen_num;
			}
			return slice_err;
		}
		const std::span<const uint8_t> slice = slice_res.value();
		if (slice.size() > lexer_->max_token_bytes()) {
			return make_pdf_error(
				ErrorCode::DecompressionLimitExceeded,
				"PDF raw stream slice exceeds maximum byte ceiling",
				tok.byte_offset,
				obj_num,
				gen_num);
		}
		PdfStream stream;
		stream.dict = std::move(*body.as_dict_mut());
		stream.raw_bytes.assign(slice.begin(), slice.end());
		stream.file_offset = static_cast<uint64_t>(slice.data() - lexer_->input().data());
		stream.raw_length = slice.size();
		body = PdfValue(std::move(stream));
	}

	if (!next_non_comment(*lexer_, tok, err)) {
		if (!err.object_num.has_value()) {
			err.object_num = obj_num;
			err.generation_num = gen_num;
		}
		return err;
	}
	if (!tok.is_keyword("endobj")) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"Expected 'endobj' keyword after indirect object body",
			tok.byte_offset,
			obj_num,
			gen_num);
	}

	return IndirectObject{
		IndirectRef{obj_num, gen_num},
		std::move(body)};
}

} // namespace recto::pdf
