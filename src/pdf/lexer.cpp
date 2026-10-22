// Binary-safe PDF 1.7 and 2.0 lexical analyzer (ISO 32000-1:2008 section 7.2,
// ISO 32000-2:2020 section 7.2) for recto-pdf (D01 T01 section 3).
#include "lexer.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <limits>
#include <new>

namespace recto::pdf {

namespace detail {
namespace {

std::atomic<size_t> g_lexer_live_bytes{0};
thread_local int g_lexer_alloc_fail_countdown = -1;

} // namespace

void* lexer_allocate(size_t bytes)
{
	if (g_lexer_alloc_fail_countdown >= 0 && g_lexer_alloc_fail_countdown-- == 0) {
		throw std::bad_alloc();
	}
	void* ptr = ::operator new(bytes);
	g_lexer_live_bytes.fetch_add(bytes, std::memory_order_relaxed);
	return ptr;
}

void lexer_deallocate(void* ptr, size_t bytes) noexcept
{
	if (ptr) {
		g_lexer_live_bytes.fetch_sub(bytes, std::memory_order_relaxed);
		::operator delete(ptr);
	}
}

size_t lexer_live_allocated_bytes() noexcept
{
	return g_lexer_live_bytes.load(std::memory_order_relaxed);
}

void set_lexer_alloc_fail_countdown(int countdown) noexcept
{
	g_lexer_alloc_fail_countdown = countdown;
}

} // namespace detail

namespace {

constexpr int hex_digit_value(uint8_t b) noexcept
{
	if (b >= '0' && b <= '9') {
		return static_cast<int>(b - '0');
	}
	if (b >= 'A' && b <= 'F') {
		return static_cast<int>(b - 'A' + 10);
	}
	if (b >= 'a' && b <= 'f') {
		return static_cast<int>(b - 'a' + 10);
	}
	return -1;
}

constexpr bool is_ascii_digit(uint8_t b) noexcept
{
	return b >= '0' && b <= '9';
}

constexpr bool is_octal_digit(uint8_t b) noexcept
{
	return b >= '0' && b <= '7';
}

enum class NumericScanKind : uint8_t {
	NotNumeric = 0,
	Integer,
	Real,
	MalformedNumber,
};

// Inspects a token made of regular characters to determine whether it is a
// valid PDF integer, a valid PDF real number, a malformed number token (such
// as "--5", "1.2.3", "+.", "-"), or a non-numeric keyword/operator.
NumericScanKind classify_numeric_token(std::string_view raw) noexcept
{
	if (raw.empty()) {
		return NumericScanKind::NotNumeric;
	}

	const uint8_t first = static_cast<uint8_t>(raw.front());
	const bool starts_like_number =
		is_ascii_digit(first) || first == '+' || first == '-' || first == '.';
	if (!starts_like_number) {
		return NumericScanKind::NotNumeric;
	}

	bool all_num_chars = true;
	for (const char ch : raw) {
		const uint8_t b = static_cast<uint8_t>(ch);
		if (!is_ascii_digit(b) && b != '+' && b != '-' && b != '.') {
			all_num_chars = false;
			break;
		}
	}
	if (!all_num_chars) {
		return NumericScanKind::NotNumeric;
	}

	size_t idx = 0;
	if (raw[idx] == '+' || raw[idx] == '-') {
		++idx;
	}
	if (idx >= raw.size()) {
		return NumericScanKind::MalformedNumber;
	}

	size_t digit_count = 0;
	size_t dot_count = 0;
	for (; idx < raw.size(); ++idx) {
		const uint8_t b = static_cast<uint8_t>(raw[idx]);
		if (is_ascii_digit(b)) {
			++digit_count;
		} else if (b == '.') {
			++dot_count;
			if (dot_count > 1) {
				return NumericScanKind::MalformedNumber;
			}
		} else {
			// Sign character '+' or '-' after index 0 (e.g. "--5" or "12-3")
			return NumericScanKind::MalformedNumber;
		}
	}

	if (digit_count == 0) {
		return NumericScanKind::MalformedNumber;
	}
	return dot_count == 0 ? NumericScanKind::Integer : NumericScanKind::Real;
}

} // namespace

Lexer::Lexer(
	std::span<const uint8_t> input,
	bool emit_comments,
	size_t max_token_bytes) noexcept
	: input_(input)
	, cursor_(0)
	, logical_cursor_(0)
	, emit_comments_(emit_comments)
	, max_token_bytes_(std::min(max_token_bytes, kMaxLexerTokenBytes))
	, lookahead_buf_(nullptr)
	, lookahead_head_(0)
	, lookahead_count_(0)
	, lookahead_capacity_(0)
{
}

void Lexer::clear_lookahead() noexcept
{
	lookahead_buf_.reset();
	lookahead_head_ = 0;
	lookahead_count_ = 0;
	lookahead_capacity_ = 0;
}

bool Lexer::ensure_lookahead_slot()
{
	if (lookahead_count_ < lookahead_capacity_) {
		return true;
	}
	if (lookahead_capacity_ >= kMaxLexerLookahead) {
		return false;
	}
	const size_t new_cap = (lookahead_capacity_ == 0)
		? 8
		: std::min(lookahead_capacity_ * 2, kMaxLexerLookahead);
	auto new_buf = std::make_unique<LookaheadEntry[]>(new_cap);
	for (size_t i = 0; i < lookahead_count_; ++i) {
		const size_t idx = (lookahead_head_ + i) % lookahead_capacity_;
		new_buf[i] = std::move(lookahead_buf_[idx]);
	}
	lookahead_buf_ = std::move(new_buf);
	lookahead_head_ = 0;
	lookahead_capacity_ = new_cap;
	return true;
}

size_t Lexer::position() const noexcept
{
	return logical_cursor_;
}

void Lexer::set_emit_comments(bool enabled) noexcept
{
	if (emit_comments_ != enabled) {
		emit_comments_ = enabled;
		record_scanned_offset(cursor_);
		cursor_ = logical_cursor_;
		clear_lookahead();
	}
}

void Lexer::set_max_token_bytes(size_t max_bytes) noexcept
{
	max_token_bytes_ = std::min(max_bytes, kMaxLexerTokenBytes);
	record_scanned_offset(cursor_);
	cursor_ = logical_cursor_;
	clear_lookahead();
}

void Lexer::seek(size_t byte_offset) noexcept
{
	record_scanned_offset(cursor_);
	cursor_ = std::min(byte_offset, input_.size());
	record_scanned_offset(cursor_);
	logical_cursor_ = cursor_;
	clear_lookahead();
}

PdfResult<Token> Lexer::next_token() noexcept
{
	const size_t saved_cursor = cursor_;
	try {
		if (lookahead_count_ > 0) {
			const size_t idx = lookahead_head_;
			const size_t next_logical = lookahead_buf_[idx].cursor_after;
			PdfResult<Token> result(std::move(lookahead_buf_[idx].token));
			lookahead_head_ = (lookahead_head_ + 1) % lookahead_capacity_;
			--lookahead_count_;
			if (lookahead_count_ == 0) {
				clear_lookahead();
			}
			logical_cursor_ = next_logical;
			return result;
		}
		auto res = read_raw_token();
		if (!res.has_value()) {
			logical_cursor_ = cursor_;
			return res;
		}
		logical_cursor_ = cursor_;
		return res;
	} catch (const std::bad_alloc&) {
		record_scanned_offset(cursor_);
		cursor_ = saved_cursor;
		return PdfError{
			ErrorCode::OutOfMemory,
			"Out of memory in Lexer::next_token",
			logical_cursor_};
	} catch (...) {
		record_scanned_offset(cursor_);
		cursor_ = saved_cursor;
		return PdfError{
			ErrorCode::InternalError,
			"Unexpected failure in Lexer::next_token",
			logical_cursor_};
	}
}

PdfResult<Token> Lexer::peek_token(size_t lookahead_index) noexcept
{
	size_t saved_cursor = cursor_;
	try {
		while (lookahead_count_ <= lookahead_index) {
			if (lookahead_count_ > 0) {
				const size_t back_idx =
					(lookahead_head_ + lookahead_count_ - 1) % lookahead_capacity_;
				if (lookahead_buf_[back_idx].token.is_eof()) {
					break;
				}
			}
			saved_cursor = cursor_;
			if (!ensure_lookahead_slot()) {
				return make_pdf_error(
					ErrorCode::DecompressionLimitExceeded,
					"Lexer lookahead limit exceeded",
					cursor_);
			}
			auto res = read_raw_token();
			if (!res.has_value()) {
				// Restore cursor_ so a failed speculative lookahead does not
				// silently consume the malformed token before next_token().
				record_scanned_offset(cursor_);
				cursor_ = saved_cursor;
				return std::move(res).error();
			}
			const size_t tail_idx =
				(lookahead_head_ + lookahead_count_) % lookahead_capacity_;
			lookahead_buf_[tail_idx].token = std::move(res).value();
			lookahead_buf_[tail_idx].cursor_after = cursor_;
			++lookahead_count_;
			saved_cursor = cursor_;
		}

		const size_t clamped_index = std::min(lookahead_index, lookahead_count_ - 1);
		const size_t target_idx =
			(lookahead_head_ + clamped_index) % lookahead_capacity_;
		return lookahead_buf_[target_idx].token;
	} catch (const std::bad_alloc&) {
		record_scanned_offset(cursor_);
		cursor_ = saved_cursor;
		return PdfError{
			ErrorCode::OutOfMemory,
			"Out of memory in Lexer::peek_token",
			logical_cursor_};
	} catch (...) {
		record_scanned_offset(cursor_);
		cursor_ = saved_cursor;
		return PdfError{
			ErrorCode::InternalError,
			"Unexpected failure in Lexer::peek_token",
			logical_cursor_};
	}
}

PdfResult<uint64_t> Lexer::consume_stream_eol() noexcept
{
	// Synchronize physical cursor with the exact byte offset immediately after
	// the last consumed token (`stream`), discarding any speculative lookahead.
	record_scanned_offset(cursor_);
	cursor_ = logical_cursor_;
	clear_lookahead();

	// Skip optional horizontal whitespace (SP 0x20, HT 0x09) on the same line
	// after the `stream` keyword.
	while (cursor_ < input_.size() &&
		(input_[cursor_] == 0x20u || input_[cursor_] == 0x09u)) {
		++cursor_;
	}

	if (cursor_ >= input_.size()) {
		logical_cursor_ = cursor_;
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"Truncated PDF stream: missing EOL after stream keyword",
			cursor_);
	}

	// ISO 32000-1 section 7.3.8.1 / ISO 32000-2 section 7.3.8.1:
	// The keyword `stream` that follows the stream dictionary shall be followed
	// by an end-of-line marker consisting of either a CARRIAGE RETURN and a
	// LINE FEED (\r\n) or just a LINE FEED (\n), and not by a single CARRIAGE
	// RETURN (\r) alone.
	if (input_[cursor_] == '\r') {
		if (cursor_ + 1 < input_.size() && input_[cursor_ + 1] == '\n') {
			cursor_ += 2;
			logical_cursor_ = cursor_;
			return static_cast<uint64_t>(cursor_);
		}
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"Bare CR after stream keyword is forbidden by ISO 32000-1 section 7.3.8.1",
			cursor_);
	}
	if (input_[cursor_] == '\n') {
		++cursor_;
		logical_cursor_ = cursor_;
		return static_cast<uint64_t>(cursor_);
	}

	return make_pdf_error(
		ErrorCode::DamagedFormat,
		"Invalid byte after stream keyword: expected LF or CRLF",
		cursor_);
}

PdfResult<Token> Lexer::read_raw_token()
{
	while (true) {
		while (cursor_ < input_.size() && is_pdf_whitespace(input_[cursor_])) {
			++cursor_;
		}

		if (cursor_ >= input_.size()) {
			Token eof_tok;
			eof_tok.kind = TokenKind::Eof;
			eof_tok.byte_offset = cursor_;
			eof_tok.byte_length = 0;
			return eof_tok;
		}

		const size_t start_pos = cursor_;
		const uint8_t b = input_[cursor_];

		if (b == '%') {
			RECTO_PDF_TRY(Token comment_tok, read_comment(start_pos));
			if (emit_comments_) {
				return comment_tok;
			}
			continue;
		}

		if (b == '[') {
			++cursor_;
			Token tok;
			tok.kind = TokenKind::ArrayOpen;
			tok.byte_offset = start_pos;
			tok.byte_length = 1;
			return tok;
		}
		if (b == ']') {
			++cursor_;
			Token tok;
			tok.kind = TokenKind::ArrayClose;
			tok.byte_offset = start_pos;
			tok.byte_length = 1;
			return tok;
		}
		if (b == '{' && syntax_ == LexerSyntax::PostScriptCalculator) {
			++cursor_;
			Token tok;
			tok.kind = TokenKind::BraceOpen;
			tok.byte_offset = start_pos;
			tok.byte_length = 1;
			return tok;
		}
		if (b == '}' && syntax_ == LexerSyntax::PostScriptCalculator) {
			++cursor_;
			Token tok;
			tok.kind = TokenKind::BraceClose;
			tok.byte_offset = start_pos;
			tok.byte_length = 1;
			return tok;
		}
		if (b == '/') {
			return read_name(start_pos);
		}
		if (b == '(') {
			return read_literal_string(start_pos);
		}
		if (b == ')') {
			++cursor_;
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"Unexpected closing parenthesis ')' outside literal string",
				start_pos);
		}
		if (b == '<') {
			if (cursor_ + 1 < input_.size() && input_[cursor_ + 1] == '<') {
				cursor_ += 2;
				Token tok;
				tok.kind = TokenKind::DictOpen;
				tok.byte_offset = start_pos;
				tok.byte_length = 2;
				return tok;
			}
			return read_hex_string(start_pos);
		}
		if (b == '>') {
			if (cursor_ + 1 < input_.size() && input_[cursor_ + 1] == '>') {
				cursor_ += 2;
				Token tok;
				tok.kind = TokenKind::DictClose;
				tok.byte_offset = start_pos;
				tok.byte_length = 2;
				return tok;
			}
			++cursor_;
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"Unexpected '>' outside hexadecimal string or dictionary delimiter",
				start_pos);
		}

		return read_number_or_keyword(start_pos);
	}
}

PdfResult<Token> Lexer::read_comment(size_t start_pos)
{
	// Skip leading '%'
	++cursor_;
	const size_t body_start = cursor_;
	while (cursor_ < input_.size()) {
		const uint8_t c = input_[cursor_];
		if (c == '\r' || c == '\n') {
			break;
		}
		++cursor_;
		if (emit_comments_ && (cursor_ - body_start) > max_token_bytes_) {
			return make_pdf_error(
				ErrorCode::DecompressionLimitExceeded,
				"PDF comment exceeds maximum token length",
				start_pos);
		}
	}
	const size_t body_end = cursor_;
	if (cursor_ < input_.size()) {
		if (input_[cursor_] == '\r') {
			++cursor_;
			if (cursor_ < input_.size() && input_[cursor_] == '\n') {
				++cursor_;
			}
		} else if (input_[cursor_] == '\n') {
			++cursor_;
		}
	}

	Token tok;
	tok.kind = TokenKind::Comment;
	tok.byte_offset = start_pos;
	tok.byte_length = cursor_ - start_pos;
	if (emit_comments_) {
		const size_t len = body_end - body_start;
		tok.text.assign(
			reinterpret_cast<const char*>(input_.data() + body_start),
			len);
	}
	return tok;
}

PdfResult<Token> Lexer::read_name(size_t start_pos)
{
	// Skip leading '/'
	++cursor_;
	TokenText decoded;
	while (cursor_ < input_.size()) {
		const uint8_t b = input_[cursor_];
		if (!is_pdf_regular(b) || (syntax_ == LexerSyntax::PostScriptCalculator && is_calculator_delimiter(b))) {
			break;
		}
		if (decoded.size() >= max_token_bytes_) {
			return make_pdf_error(
				ErrorCode::DecompressionLimitExceeded,
				"PDF name token exceeds maximum byte ceiling",
				start_pos);
		}
		if (b == '#') {
			if (cursor_ + 2 >= input_.size()) {
				cursor_ = input_.size();
				return make_pdf_error(
					ErrorCode::DamagedFormat,
					"Truncated #XX hex escape in PDF name object",
					start_pos);
			}
			const int hi = hex_digit_value(input_[cursor_ + 1]);
			const int lo = hex_digit_value(input_[cursor_ + 2]);
			if (hi < 0 || lo < 0) {
				cursor_ += 3;
				return make_pdf_error(
					ErrorCode::DamagedFormat,
					"Invalid #XX hex escape in PDF name object",
					start_pos);
			}
			const uint8_t escaped_byte = static_cast<uint8_t>((hi << 4) | lo);
			if (escaped_byte == 0x00u) {
				cursor_ += 3;
				return make_pdf_error(
					ErrorCode::DamagedFormat,
					"Null byte escape /#00 is forbidden in PDF name objects",
					start_pos);
			}
			decoded.push_back(static_cast<char>(escaped_byte));
			cursor_ += 3;
		} else {
			decoded.push_back(static_cast<char>(b));
			++cursor_;
		}
	}

	Token tok;
	tok.kind = TokenKind::Name;
	tok.byte_offset = start_pos;
	tok.byte_length = cursor_ - start_pos;
	tok.text = std::move(decoded);
	return tok;
}

PdfResult<Token> Lexer::read_literal_string(size_t start_pos)
{
	// Skip opening '('
	++cursor_;
	size_t paren_depth = 1;
	TokenBytes out;

	auto append_checked = [&](uint8_t byte) -> PdfResult<void> {
		if (out.size() >= max_token_bytes_) {
			return make_pdf_error(
				ErrorCode::DecompressionLimitExceeded,
				"PDF literal string exceeds maximum byte ceiling",
				start_pos);
		}
		out.push_back(byte);
		return PdfResult<void>::ok();
	};

	while (cursor_ < input_.size()) {
		const uint8_t b = input_[cursor_++];
		if (b == '(') {
			++paren_depth;
			RECTO_PDF_RETURN_IF_ERROR(append_checked('('));
		} else if (b == ')') {
			--paren_depth;
			if (paren_depth == 0) {
				Token tok;
				tok.kind = TokenKind::LiteralString;
				tok.byte_offset = start_pos;
				tok.byte_length = cursor_ - start_pos;
				tok.bytes = std::move(out);
				return tok;
			}
			RECTO_PDF_RETURN_IF_ERROR(append_checked(')'));
		} else if (b == '\\') {
			if (cursor_ >= input_.size()) {
				return make_pdf_error(
					ErrorCode::DamagedFormat,
					"Truncated backslash escape in PDF literal string",
					start_pos);
			}
			const uint8_t esc = input_[cursor_++];
			switch (esc) {
			case 'n':
				RECTO_PDF_RETURN_IF_ERROR(append_checked('\n'));
				break;
			case 'r':
				RECTO_PDF_RETURN_IF_ERROR(append_checked('\r'));
				break;
			case 't':
				RECTO_PDF_RETURN_IF_ERROR(append_checked('\t'));
				break;
			case 'b':
				RECTO_PDF_RETURN_IF_ERROR(append_checked('\b'));
				break;
			case 'f':
				RECTO_PDF_RETURN_IF_ERROR(append_checked('\f'));
				break;
			case '(':
				RECTO_PDF_RETURN_IF_ERROR(append_checked('('));
				break;
			case ')':
				RECTO_PDF_RETURN_IF_ERROR(append_checked(')'));
				break;
			case '\\':
				RECTO_PDF_RETURN_IF_ERROR(append_checked('\\'));
				break;
			case '\r':
				// Backslash followed by CR or CRLF is a line continuation (ignored).
				if (cursor_ < input_.size() && input_[cursor_] == '\n') {
					++cursor_;
				}
				break;
			case '\n':
				// Backslash followed by LF is a line continuation (ignored).
				break;
			default:
				if (is_octal_digit(esc)) {
					uint32_t oct_val = static_cast<uint32_t>(esc - '0');
					for (int k = 0; k < 2 && cursor_ < input_.size() && is_octal_digit(input_[cursor_]); ++k) {
						oct_val = (oct_val << 3u) | static_cast<uint32_t>(input_[cursor_] - '0');
						++cursor_;
					}
					// ISO 32000-1 section 7.3.4.2: high-order overflow in \ddd is ignored.
					RECTO_PDF_RETURN_IF_ERROR(append_checked(static_cast<uint8_t>(oct_val & 0xFFu)));
				} else {
					// ISO 32000-1 section 7.3.4.2: if the character following '\'
					// is not one of the recognized escapes, the '\' is ignored.
					RECTO_PDF_RETURN_IF_ERROR(append_checked(esc));
				}
				break;
			}
		} else if (b == '\r') {
			// ISO 32000-1 section 7.3.4.2: an end-of-line marker (\r, \n, or \r\n)
			// appearing within a literal string without a preceding backslash is
			// normalized to a single \n (0x0A).
			if (cursor_ < input_.size() && input_[cursor_] == '\n') {
				++cursor_;
			}
			RECTO_PDF_RETURN_IF_ERROR(append_checked('\n'));
		} else {
			RECTO_PDF_RETURN_IF_ERROR(append_checked(b));
		}
	}

	return make_pdf_error(
		ErrorCode::DamagedFormat,
		"Unterminated PDF literal string",
		start_pos);
}

PdfResult<Token> Lexer::read_hex_string(size_t start_pos)
{
	// Skip opening '<'
	++cursor_;
	TokenBytes out;
	int pending_high_nibble = -1;

	while (cursor_ < input_.size()) {
		const uint8_t b = input_[cursor_++];
		if (b == '>') {
			if (pending_high_nibble >= 0) {
				if (out.size() >= max_token_bytes_) {
					return make_pdf_error(
						ErrorCode::DecompressionLimitExceeded,
						"PDF hexadecimal string exceeds maximum byte ceiling",
						start_pos);
				}
				// ISO 32000-1 section 7.3.4.3: if the final digit of a hexadecimal
				// string is missing (odd number of digits), the final digit shall
				// be assumed to be 0.
				out.push_back(static_cast<uint8_t>(pending_high_nibble << 4));
			}
			Token tok;
			tok.kind = TokenKind::HexString;
			tok.byte_offset = start_pos;
			tok.byte_length = cursor_ - start_pos;
			tok.bytes = std::move(out);
			return tok;
		}
		if (is_pdf_whitespace(b)) {
			continue;
		}
		const int nibble = hex_digit_value(b);
		if (nibble < 0) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"Invalid non-hexadecimal character in PDF hex string",
				cursor_ - 1);
		}
		if (pending_high_nibble < 0) {
			pending_high_nibble = nibble;
		} else {
			if (out.size() >= max_token_bytes_) {
				return make_pdf_error(
					ErrorCode::DecompressionLimitExceeded,
					"PDF hexadecimal string exceeds maximum byte ceiling",
					start_pos);
			}
			out.push_back(static_cast<uint8_t>((pending_high_nibble << 4) | nibble));
			pending_high_nibble = -1;
		}
	}

	return make_pdf_error(
		ErrorCode::DamagedFormat,
		"Unterminated PDF hexadecimal string",
		start_pos);
}

PdfResult<Token> Lexer::read_number_or_keyword(size_t start_pos)
{
	while (cursor_ < input_.size() && is_pdf_regular(input_[cursor_]) &&
		!(syntax_ == LexerSyntax::PostScriptCalculator && is_calculator_delimiter(input_[cursor_]))) {
		if ((cursor_ - start_pos) >= max_token_bytes_) {
			return make_pdf_error(
				ErrorCode::DecompressionLimitExceeded,
				"PDF token exceeds maximum byte ceiling",
				start_pos);
		}
		++cursor_;
	}

	const size_t len = cursor_ - start_pos;
	const std::string_view raw(
		reinterpret_cast<const char*>(input_.data() + start_pos),
		len);

	if (raw == "true") {
		Token tok;
		tok.kind = TokenKind::Boolean;
		tok.byte_offset = start_pos;
		tok.byte_length = len;
		tok.bool_value = true;
		return tok;
	}
	if (raw == "false") {
		Token tok;
		tok.kind = TokenKind::Boolean;
		tok.byte_offset = start_pos;
		tok.byte_length = len;
		tok.bool_value = false;
		return tok;
	}
	if (raw == "null") {
		Token tok;
		tok.kind = TokenKind::Null;
		tok.byte_offset = start_pos;
		tok.byte_length = len;
		return tok;
	}

	const NumericScanKind num_kind = classify_numeric_token(raw);
	if (num_kind == NumericScanKind::MalformedNumber) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"Malformed numeric token in PDF stream",
			start_pos);
	}

	if (num_kind == NumericScanKind::Integer) {
		std::string_view digits = raw;
		if (!digits.empty() && digits.front() == '+') {
			digits.remove_prefix(1);
		}
		int64_t val = 0;
		const auto [ptr, ec] = std::from_chars(
			digits.data(),
			digits.data() + digits.size(),
			val,
			10);
		if (ec == std::errc::result_out_of_range) {
			// Clamp signed 64-bit integer overflow without undefined behavior
			// and record a diagnostic warning so callers can detect or clamp.
			record_diagnostic(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				start_pos);
			val = (!digits.empty() && digits.front() == '-')
				? std::numeric_limits<int64_t>::min()
				: std::numeric_limits<int64_t>::max();
		} else if (ec != std::errc{} || ptr != digits.data() + digits.size()) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"Invalid integer token in PDF stream",
				start_pos);
		}
		Token tok;
		tok.kind = TokenKind::Integer;
		tok.byte_offset = start_pos;
		tok.byte_length = len;
		tok.int_value = val;
		tok.real_value = static_cast<double>(val);
		return tok;
	}

	if (num_kind == NumericScanKind::Real) {
		std::string_view real_str = raw;
		bool negative = false;
		if (!real_str.empty() && (real_str.front() == '+' || real_str.front() == '-')) {
			negative = (real_str.front() == '-');
			real_str.remove_prefix(1);
		}
		double val = 0.0;
		const auto [ptr, ec] = std::from_chars(
			real_str.data(),
			real_str.data() + real_str.size(),
			val,
			std::chars_format::fixed);
		if (ec == std::errc::result_out_of_range) {
			// Underflow flushes to 0.0; overflow is rejected as DamagedFormat.
			bool has_nonzero_int_part = false;
			for (const char c : real_str) {
				if (c == '.') {
					break;
				}
				if (c >= '1' && c <= '9') {
					has_nonzero_int_part = true;
					break;
				}
			}
			if (has_nonzero_int_part) {
				return make_pdf_error(
					ErrorCode::DamagedFormat,
					"Real number token overflows double range",
					start_pos);
			}
			val = 0.0;
		} else if (ec != std::errc{} || ptr != real_str.data() + real_str.size() || !std::isfinite(val)) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"Invalid real number token in PDF stream",
				start_pos);
		}
		if (negative) {
			val = -val;
		}
		Token tok;
		tok.kind = TokenKind::Real;
		tok.byte_offset = start_pos;
		tok.byte_length = len;
		tok.real_value = val;
		return tok;
	}

	Token tok;
	tok.kind = TokenKind::Keyword;
	tok.byte_offset = start_pos;
	tok.byte_length = len;
	tok.text.assign(raw.data(), raw.size());
	return tok;
}

} // namespace recto::pdf
