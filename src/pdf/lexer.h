// Binary-safe PDF 1.7 and 2.0 lexical analyzer (ISO 32000-1:2008 section 7.2,
// ISO 32000-2:2020 section 7.2) for recto-pdf (D01 T01 section 3).
//
// Freeze contract (foundation plan section 4.2):
// Lexer operates purely over a read-only std::span<const uint8_t> cursor with
// explicit bounds checks before every byte read, never reading past
// span.size() even on truncated or hostile inputs, and holds zero shared state.
#pragma once

#include "error.h"
#include "object.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace recto::pdf {

// Maximum byte length of a single decoded literal string, hex string, name,
// keyword, or comment token (256 MB, matching foundation plan section 9).
inline constexpr size_t kMaxLexerTokenBytes = 256ull * 1024ull * 1024ull;

enum class TokenKind : uint8_t {
	Eof = 0,
	Boolean,
	Null,
	Integer,
	Real,
	Name,
	LiteralString,
	HexString,
	ArrayOpen,    // '['
	ArrayClose,   // ']'
	DictOpen,     // '<<'
	DictClose,    // '>>'
	BraceOpen,    // '{'
	BraceClose,   // '}'
	Keyword,      // 'obj', 'endobj', 'stream', 'endstream', 'xref', 'trailer', 'startxref', 'R', or content operator
	Comment,      // '%...' (emitted when emit_comments is enabled)
};

struct Token {
	TokenKind kind{TokenKind::Eof};
	uint64_t byte_offset{0};
	uint64_t byte_length{0};
	bool bool_value{false};
	int64_t int_value{0};
	double real_value{0.0};
	std::string text;           // decoded Name, Keyword, or Comment text
	std::vector<uint8_t> bytes; // decoded LiteralString or HexString bytes

	[[nodiscard]] bool is_eof() const noexcept
	{
		return kind == TokenKind::Eof;
	}

	[[nodiscard]] bool is_keyword(std::string_view kw) const noexcept
	{
		return kind == TokenKind::Keyword && text == kw;
	}
};

class Lexer {
public:
	explicit Lexer(
		std::span<const uint8_t> input,
		bool emit_comments = false) noexcept;

	[[nodiscard]] std::span<const uint8_t> input() const noexcept
	{
		return input_;
	}

	[[nodiscard]] size_t size() const noexcept
	{
		return input_.size();
	}

	[[nodiscard]] size_t position() const noexcept;

	[[nodiscard]] bool at_eof() const noexcept
	{
		return position() >= input_.size();
	}

	void set_emit_comments(bool enabled) noexcept;
	[[nodiscard]] bool emit_comments() const noexcept
	{
		return emit_comments_;
	}

	// Seeks the lexer cursor to byte_offset (clamped to input_.size()) and
	// clears any cached lookahead tokens.
	void seek(size_t byte_offset) noexcept;

	// Consumes and returns the next lexical token.
	[[nodiscard]] PdfResult<Token> next_token() noexcept;

	// Peeks ahead lookahead_index tokens (0 = next token, 1 = second token,
	// 2 = third token for `<int> <int> R` indirect-reference disambiguation)
	// without advancing the logical token stream.
	[[nodiscard]] PdfResult<Token> peek_token(size_t lookahead_index = 0) noexcept;

	// Enforces the ISO 32000-1 section 7.3.8.1 / ISO 32000-2 section 7.3.8.1
	// `stream` keyword end-of-line rule:
	// - Skips optional horizontal spaces (0x20 SP, 0x09 HT) after `stream`.
	// - Requires a single EOL marker (`\r\n` or `\n`, or bare `\r` only as a
	//   lenient fallback when not followed by `\n`, consuming 1 byte so the
	//   immediately following stream byte 0 is preserved).
	// - Leaves position() at byte 0 of the stream payload and returns that
	//   byte offset.
	[[nodiscard]] PdfResult<uint64_t> consume_stream_eol() noexcept;

	[[nodiscard]] static constexpr bool is_pdf_whitespace(uint8_t b) noexcept
	{
		return b == 0x00u || // NUL
			b == 0x09u ||    // HT
			b == 0x0Au ||    // LF
			b == 0x0Cu ||    // FF
			b == 0x0Du ||    // CR
			b == 0x20u;      // SP
	}

	[[nodiscard]] static constexpr bool is_pdf_delimiter(uint8_t b) noexcept
	{
		return b == '(' || b == ')' ||
			b == '<' || b == '>' ||
			b == '[' || b == ']' ||
			b == '{' || b == '}' ||
			b == '/' || b == '%';
	}

	[[nodiscard]] static constexpr bool is_pdf_regular(uint8_t b) noexcept
	{
		return !is_pdf_whitespace(b) && !is_pdf_delimiter(b);
	}

private:
	[[nodiscard]] PdfResult<Token> read_raw_token() noexcept;
	[[nodiscard]] PdfResult<Token> read_comment(size_t start_pos) noexcept;
	[[nodiscard]] PdfResult<Token> read_name(size_t start_pos) noexcept;
	[[nodiscard]] PdfResult<Token> read_literal_string(size_t start_pos) noexcept;
	[[nodiscard]] PdfResult<Token> read_hex_string(size_t start_pos) noexcept;
	[[nodiscard]] PdfResult<Token> read_number_or_keyword(size_t start_pos) noexcept;

	std::span<const uint8_t> input_{};
	size_t cursor_{0};
	bool emit_comments_{false};
	std::deque<Token> lookahead_{};
};

} // namespace recto::pdf
