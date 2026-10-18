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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace recto::pdf {

namespace detail {

[[nodiscard]] void* lexer_allocate(size_t bytes);
void lexer_deallocate(void* ptr, size_t bytes) noexcept;
[[nodiscard]] size_t lexer_live_allocated_bytes() noexcept;
void set_lexer_alloc_fail_countdown(int countdown) noexcept;

class ScopedLexerAllocFaultInjector {
public:
	explicit ScopedLexerAllocFaultInjector(int fail_after_n_allocations) noexcept
	{
		set_lexer_alloc_fail_countdown(fail_after_n_allocations);
	}

	ScopedLexerAllocFaultInjector(const ScopedLexerAllocFaultInjector&) = delete;
	ScopedLexerAllocFaultInjector& operator=(const ScopedLexerAllocFaultInjector&) = delete;

	~ScopedLexerAllocFaultInjector() noexcept
	{
		set_lexer_alloc_fail_countdown(-1);
	}
};

} // namespace detail

// Maximum byte length of a single decoded literal string, hex string, name,
// keyword, or comment token (256 MB, matching foundation plan section 9).
inline constexpr size_t kMaxLexerTokenBytes = 256ull * 1024ull * 1024ull;

// Maximum number of speculative lookahead tokens cached by Lexer::peek_token().
inline constexpr size_t kMaxLexerLookahead = 64;

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

// Proxy-free character buffer for Token::text whose default and move
// constructors make zero heap allocations in both Debug (/MDd) and Release
// (/MD), while copy/growth operations throw std::bad_alloc cleanly into
// Lexer's try/catch boundary without triggering noexcept termination.
class TokenText {
public:
	using value_type = char;
	using size_type = size_t;
	using difference_type = ptrdiff_t;
	using const_iterator = const char*;
	using iterator = char*;

	constexpr TokenText() noexcept = default;

	TokenText(std::string_view sv)
	{
		assign(sv.data(), sv.size());
	}

	TokenText(const char* s)
	{
		if (s) {
			assign(s, std::char_traits<char>::length(s));
		}
	}

	TokenText(const std::string& s)
	{
		assign(s.data(), s.size());
	}

	TokenText(const TokenText& other)
	{
		assign(other.data_, other.size_);
	}

	TokenText(TokenText&& other) noexcept
		: data_(other.data_)
		, size_(other.size_)
		, capacity_(other.capacity_)
	{
		other.data_ = nullptr;
		other.size_ = 0;
		other.capacity_ = 0;
	}

	TokenText& operator=(const TokenText& other)
	{
		if (this != &other) {
			assign(other.data_, other.size_);
		}
		return *this;
	}

	TokenText& operator=(TokenText&& other) noexcept
	{
		if (this != &other) {
			reset();
			data_ = other.data_;
			size_ = other.size_;
			capacity_ = other.capacity_;
			other.data_ = nullptr;
			other.size_ = 0;
			other.capacity_ = 0;
		}
		return *this;
	}

	TokenText& operator=(std::string_view sv)
	{
		assign(sv.data(), sv.size());
		return *this;
	}

	TokenText& operator=(const char* s)
	{
		if (s) {
			assign(s, std::char_traits<char>::length(s));
		} else {
			clear();
		}
		return *this;
	}

	TokenText& operator=(const std::string& s)
	{
		assign(s.data(), s.size());
		return *this;
	}

	~TokenText() noexcept
	{
		reset();
	}

	void clear() noexcept
	{
		size_ = 0;
		if (data_) {
			data_[0] = '\0';
		}
	}

	void reset() noexcept
	{
		if (data_) {
			detail::lexer_deallocate(data_, capacity_ + 1);
			data_ = nullptr;
		}
		size_ = 0;
		capacity_ = 0;
	}

	void reserve(size_t new_cap)
	{
		if (new_cap <= capacity_) {
			return;
		}
		if (new_cap == static_cast<size_t>(-1)) {
			throw std::bad_alloc();
		}
		char* new_buf = static_cast<char*>(detail::lexer_allocate(new_cap + 1));
		if (size_ > 0 && data_) {
			std::memcpy(new_buf, data_, size_);
		}
		new_buf[size_] = '\0';
		if (data_) {
			detail::lexer_deallocate(data_, capacity_ + 1);
		}
		data_ = new_buf;
		capacity_ = new_cap;
	}

	void assign(const char* ptr, size_t len)
	{
		if (!ptr || len == 0) {
			clear();
			return;
		}
		if (len > capacity_) {
			if (len == static_cast<size_t>(-1)) {
				throw std::bad_alloc();
			}
			char* new_buf = static_cast<char*>(detail::lexer_allocate(len + 1));
			std::memcpy(new_buf, ptr, len);
			new_buf[len] = '\0';
			if (data_) {
				detail::lexer_deallocate(data_, capacity_ + 1);
			}
			data_ = new_buf;
			capacity_ = len;
			size_ = len;
			return;
		}
		std::memmove(data_, ptr, len);
		data_[len] = '\0';
		size_ = len;
	}

	void assign(std::string_view sv)
	{
		assign(sv.data(), sv.size());
	}

	void push_back(char ch)
	{
		if (size_ == capacity_) {
			if (capacity_ >= static_cast<size_t>(-1) - 1) {
				throw std::bad_alloc();
			}
			const size_t next_cap = (capacity_ == 0)
				? 16
				: ((capacity_ > (static_cast<size_t>(-1) - 1) / 2)
					  ? (static_cast<size_t>(-1) - 1)
					  : (capacity_ * 2));
			reserve(next_cap);
		}
		data_[size_++] = ch;
		data_[size_] = '\0';
	}

	[[nodiscard]] constexpr bool empty() const noexcept
	{
		return size_ == 0;
	}

	[[nodiscard]] constexpr size_t size() const noexcept
	{
		return size_;
	}

	[[nodiscard]] constexpr size_t length() const noexcept
	{
		return size_;
	}

	[[nodiscard]] constexpr size_t capacity() const noexcept
	{
		return capacity_;
	}

	[[nodiscard]] constexpr const char* data() const noexcept
	{
		return data_ ? data_ : "";
	}

	[[nodiscard]] constexpr char* data() noexcept
	{
		return data_;
	}

	[[nodiscard]] constexpr const char* c_str() const noexcept
	{
		return data_ ? data_ : "";
	}

	[[nodiscard]] constexpr const_iterator begin() const noexcept
	{
		return data();
	}

	[[nodiscard]] constexpr const_iterator end() const noexcept
	{
		return data() + size_;
	}

	[[nodiscard]] constexpr char operator[](size_t idx) const noexcept
	{
		return data_[idx];
	}

	[[nodiscard]] constexpr char& operator[](size_t idx) noexcept
	{
		return data_[idx];
	}

	[[nodiscard]] constexpr char front() const noexcept
	{
		return data_[0];
	}

	[[nodiscard]] constexpr char back() const noexcept
	{
		return data_[size_ - 1];
	}

	[[nodiscard]] constexpr std::string_view view() const noexcept
	{
		return {data(), size_};
	}

	constexpr operator std::string_view() const noexcept
	{
		return view();
	}

	operator std::string() const
	{
		return std::string(data(), size_);
	}

	friend constexpr bool operator==(const TokenText& a, const TokenText& b) noexcept
	{
		return a.view() == b.view();
	}

	friend constexpr bool operator==(const TokenText& a, std::string_view b) noexcept
	{
		return a.view() == b;
	}

	friend constexpr bool operator==(std::string_view a, const TokenText& b) noexcept
	{
		return a == b.view();
	}

	friend constexpr bool operator==(const TokenText& a, const char* b) noexcept
	{
		return a.view() == (b ? std::string_view(b) : std::string_view{});
	}

	friend constexpr bool operator==(const char* a, const TokenText& b) noexcept
	{
		return (a ? std::string_view(a) : std::string_view{}) == b.view();
	}

	friend bool operator==(const TokenText& a, const std::string& b) noexcept
	{
		return a.view() == std::string_view(b);
	}

	friend bool operator==(const std::string& a, const TokenText& b) noexcept
	{
		return std::string_view(a) == b.view();
	}

	friend std::ostream& operator<<(std::ostream& os, const TokenText& s)
	{
		return os.write(s.data(), static_cast<std::streamsize>(s.size()));
	}

private:
	char* data_{nullptr};
	size_t size_{0};
	size_t capacity_{0};
};

// Proxy-free byte buffer for Token::bytes whose default and move constructors
// make zero heap allocations in both Debug (/MDd) and Release (/MD), while
// copy/growth operations throw std::bad_alloc cleanly into Lexer's try/catch.
class TokenBytes {
public:
	using value_type = uint8_t;
	using size_type = size_t;
	using difference_type = ptrdiff_t;
	using const_iterator = const uint8_t*;
	using iterator = uint8_t*;

	constexpr TokenBytes() noexcept = default;

	TokenBytes(std::span<const uint8_t> sp)
	{
		assign(sp.data(), sp.size());
	}

	TokenBytes(const std::vector<uint8_t>& vec)
	{
		assign(vec.data(), vec.size());
	}

	TokenBytes(const TokenBytes& other)
	{
		assign(other.data_, other.size_);
	}

	TokenBytes(TokenBytes&& other) noexcept
		: data_(other.data_)
		, size_(other.size_)
		, capacity_(other.capacity_)
	{
		other.data_ = nullptr;
		other.size_ = 0;
		other.capacity_ = 0;
	}

	TokenBytes& operator=(const TokenBytes& other)
	{
		if (this != &other) {
			assign(other.data_, other.size_);
		}
		return *this;
	}

	TokenBytes& operator=(TokenBytes&& other) noexcept
	{
		if (this != &other) {
			reset();
			data_ = other.data_;
			size_ = other.size_;
			capacity_ = other.capacity_;
			other.data_ = nullptr;
			other.size_ = 0;
			other.capacity_ = 0;
		}
		return *this;
	}

	TokenBytes& operator=(std::span<const uint8_t> sp)
	{
		assign(sp.data(), sp.size());
		return *this;
	}

	TokenBytes& operator=(const std::vector<uint8_t>& vec)
	{
		assign(vec.data(), vec.size());
		return *this;
	}

	~TokenBytes() noexcept
	{
		reset();
	}

	void clear() noexcept
	{
		size_ = 0;
	}

	void reset() noexcept
	{
		if (data_) {
			detail::lexer_deallocate(data_, capacity_);
			data_ = nullptr;
		}
		size_ = 0;
		capacity_ = 0;
	}

	void reserve(size_t new_cap)
	{
		if (new_cap <= capacity_) {
			return;
		}
		uint8_t* new_buf = static_cast<uint8_t*>(detail::lexer_allocate(new_cap));
		if (size_ > 0 && data_) {
			std::memcpy(new_buf, data_, size_);
		}
		if (data_) {
			detail::lexer_deallocate(data_, capacity_);
		}
		data_ = new_buf;
		capacity_ = new_cap;
	}

	void assign(const uint8_t* ptr, size_t len)
	{
		if (!ptr || len == 0) {
			clear();
			return;
		}
		if (len > capacity_) {
			uint8_t* new_buf = static_cast<uint8_t*>(detail::lexer_allocate(len));
			std::memcpy(new_buf, ptr, len);
			if (data_) {
				detail::lexer_deallocate(data_, capacity_);
			}
			data_ = new_buf;
			capacity_ = len;
			size_ = len;
			return;
		}
		std::memmove(data_, ptr, len);
		size_ = len;
	}

	void push_back(uint8_t b)
	{
		if (size_ == capacity_) {
			if (capacity_ == static_cast<size_t>(-1)) {
				throw std::bad_alloc();
			}
			const size_t next_cap = (capacity_ == 0)
				? 16
				: ((capacity_ > static_cast<size_t>(-1) / 2)
					  ? static_cast<size_t>(-1)
					  : (capacity_ * 2));
			reserve(next_cap);
		}
		data_[size_++] = b;
	}

	[[nodiscard]] constexpr bool empty() const noexcept
	{
		return size_ == 0;
	}

	[[nodiscard]] constexpr size_t size() const noexcept
	{
		return size_;
	}

	[[nodiscard]] constexpr size_t capacity() const noexcept
	{
		return capacity_;
	}

	[[nodiscard]] constexpr const uint8_t* data() const noexcept
	{
		return data_;
	}

	[[nodiscard]] constexpr uint8_t* data() noexcept
	{
		return data_;
	}

	[[nodiscard]] constexpr const_iterator begin() const noexcept
	{
		return data_;
	}

	[[nodiscard]] constexpr const_iterator end() const noexcept
	{
		return data_ + size_;
	}

	[[nodiscard]] constexpr iterator begin() noexcept
	{
		return data_;
	}

	[[nodiscard]] constexpr iterator end() noexcept
	{
		return data_ + size_;
	}

	[[nodiscard]] constexpr uint8_t operator[](size_t idx) const noexcept
	{
		return data_[idx];
	}

	[[nodiscard]] constexpr uint8_t& operator[](size_t idx) noexcept
	{
		return data_[idx];
	}

	[[nodiscard]] constexpr uint8_t front() const noexcept
	{
		return data_[0];
	}

	[[nodiscard]] constexpr uint8_t back() const noexcept
	{
		return data_[size_ - 1];
	}

	[[nodiscard]] constexpr std::span<const uint8_t> span() const noexcept
	{
		return {data_, size_};
	}

	constexpr operator std::span<const uint8_t>() const noexcept
	{
		return span();
	}

	operator std::vector<uint8_t>() const
	{
		return std::vector<uint8_t>(begin(), end());
	}

	friend bool operator==(const TokenBytes& a, const TokenBytes& b) noexcept
	{
		return a.size_ == b.size_ &&
			(a.size_ == 0 || std::memcmp(a.data_, b.data_, a.size_) == 0);
	}

	friend bool operator==(const TokenBytes& a, const std::vector<uint8_t>& b) noexcept
	{
		return a.size_ == b.size() &&
			(a.size_ == 0 || std::memcmp(a.data_, b.data(), a.size_) == 0);
	}

	friend bool operator==(const std::vector<uint8_t>& a, const TokenBytes& b) noexcept
	{
		return b == a;
	}

	friend bool operator==(const TokenBytes& a, std::span<const uint8_t> b) noexcept
	{
		return a.size_ == b.size() &&
			(a.size_ == 0 || std::memcmp(a.data_, b.data(), a.size_) == 0);
	}

	friend bool operator==(std::span<const uint8_t> a, const TokenBytes& b) noexcept
	{
		return b == a;
	}

private:
	uint8_t* data_{nullptr};
	size_t size_{0};
	size_t capacity_{0};
};

struct Token {
	TokenKind kind{TokenKind::Eof};
	uint64_t byte_offset{0};
	uint64_t byte_length{0};
	bool bool_value{false};
	int64_t int_value{0};
	double real_value{0.0};
	TokenText text{};   // decoded Name, Keyword, or Comment text
	TokenBytes bytes{}; // decoded LiteralString or HexString bytes

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
		bool emit_comments = false,
		size_t max_token_bytes = kMaxLexerTokenBytes) noexcept;

	[[nodiscard]] std::span<const uint8_t> input() const noexcept
	{
		return input_;
	}

	[[nodiscard]] size_t size() const noexcept
	{
		return input_.size();
	}

	// Returns the byte offset immediately after the last consumed token (or the
	// seek target when no token has been consumed since construction/seek).
	[[nodiscard]] size_t position() const noexcept;

	// Returns the furthest byte offset physically scanned by this Lexer
	// instance (including speculative lookahead in peek_token and stream
	// terminator scans).
	[[nodiscard]] size_t max_scanned_offset() const noexcept
	{
		return std::max(max_scanned_offset_, cursor_);
	}

	void record_scanned_offset(size_t byte_offset) noexcept
	{
		max_scanned_offset_ = std::max(
			max_scanned_offset_,
			std::min(byte_offset, input_.size()));
	}

	[[nodiscard]] bool at_eof() const noexcept
	{
		return position() >= input_.size();
	}

	void set_emit_comments(bool enabled) noexcept;
	[[nodiscard]] bool emit_comments() const noexcept
	{
		return emit_comments_;
	}

	void set_max_token_bytes(size_t max_bytes) noexcept;
	[[nodiscard]] size_t max_token_bytes() const noexcept
	{
		return max_token_bytes_;
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
	// - Requires a single EOL marker (`\r\n` or `\n`, never bare `\r`).
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
	struct LookaheadEntry {
		Token token;
		size_t cursor_after{0};

		static void* operator new[](size_t bytes)
		{
			return detail::lexer_allocate(bytes);
		}

		static void operator delete[](void* ptr, size_t bytes) noexcept
		{
			detail::lexer_deallocate(ptr, bytes);
		}
	};

	void clear_lookahead() noexcept;
	[[nodiscard]] bool ensure_lookahead_slot();

	[[nodiscard]] PdfResult<Token> read_raw_token();
	[[nodiscard]] PdfResult<Token> read_comment(size_t start_pos);
	[[nodiscard]] PdfResult<Token> read_name(size_t start_pos);
	[[nodiscard]] PdfResult<Token> read_literal_string(size_t start_pos);
	[[nodiscard]] PdfResult<Token> read_hex_string(size_t start_pos);
	[[nodiscard]] PdfResult<Token> read_number_or_keyword(size_t start_pos);

	std::span<const uint8_t> input_{};
	size_t cursor_{0};
	size_t logical_cursor_{0};
	size_t max_scanned_offset_{0};
	bool emit_comments_{false};
	size_t max_token_bytes_{kMaxLexerTokenBytes};
	std::unique_ptr<LookaheadEntry[]> lookahead_buf_{};
	size_t lookahead_head_{0};
	size_t lookahead_count_{0};
	size_t lookahead_capacity_{0};
};

} // namespace recto::pdf
