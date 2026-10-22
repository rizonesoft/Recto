// Bounded recursive-descent PDF 1.7 and 2.0 object and stream parser
// (ISO 32000-1:2008 section 7.3, ISO 32000-2:2020 section 7.3) for recto-pdf
// (D01 T01 section 4).
//
// Freeze contract (foundation plan section 4.2):
// Parser holds all state on its local call frame, enforces the 64-level
// recursion cap on every '[' and '<<' entry, enforces the 1,000,000-element
// container ceiling, and never mutates shared static state.
#pragma once

#include "error.h"
#include "lexer.h"
#include "object.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace recto::pdf {

// Maximum nesting depth across nested PDF arrays ('[...]') and dictionaries
// ('<<...>>') per foundation plan section 9 and D01 T01 section 4.
inline constexpr size_t kMaxParserRecursionDepth = 64;

// Maximum number of elements in a single PDF array or key/value pairs in a
// single PDF dictionary to guard against container amplification attacks.
inline constexpr size_t kMaxContainerElements = 1'000'000;

// Maximum number of values one top-level parse (parse_direct_object, parse_array, parse_dictionary or
// parse_indirect_object) may create across all its containers, so no object costs more memory than the largest
// single container already could; many containers each under kMaxContainerElements cannot add up past it.
inline constexpr size_t kMaxParsedValues = kMaxContainerElements;

// Maximum decoded string, name and dictionary key bytes one top-level parse may hold, so no object holds more
// payload than the largest single token already could (foundation plan section 9's byte caps).
inline constexpr size_t kMaxParsedBytes = kMaxLexerTokenBytes;

struct IndirectObject {
	IndirectRef ref{};
	PdfValue value{};
};

// Optional callback used by Parser::parse_stream_slice and
// Parser::parse_indirect_object when a stream dictionary's /Length entry is an
// indirect reference (e.g. /Length 15 0 R).
using LengthResolver = std::function<PdfResult<int64_t>(IndirectRef)>;

class Parser {
public:
	explicit Parser(
		Lexer& lexer,
		size_t max_recursion_depth = kMaxParserRecursionDepth,
		size_t max_container_elements = kMaxContainerElements) noexcept;

	explicit Parser(
		std::span<const uint8_t> input,
		size_t max_recursion_depth = kMaxParserRecursionDepth,
		size_t max_container_elements = kMaxContainerElements) noexcept;

	Parser(const Parser&) = delete;
	Parser& operator=(const Parser&) = delete;

	[[nodiscard]] Lexer& lexer() noexcept
	{
		return *lexer_;
	}

	[[nodiscard]] const Lexer& lexer() const noexcept
	{
		return *lexer_;
	}

	[[nodiscard]] size_t position() const noexcept
	{
		return lexer_->position();
	}

	void seek(size_t byte_offset) noexcept
	{
		lexer_->seek(byte_offset);
	}

	void set_max_recursion_depth(size_t max_depth) noexcept;
	[[nodiscard]] size_t max_recursion_depth() const noexcept
	{
		return max_recursion_depth_;
	}

	void set_max_total_values(size_t max_values) noexcept
	{
		max_total_values_ = max_values;
	}
	[[nodiscard]] size_t max_total_values() const noexcept
	{
		return max_total_values_;
	}

	void set_max_total_bytes(size_t max_bytes) noexcept
	{
		max_total_bytes_ = max_bytes;
	}
	[[nodiscard]] size_t max_total_bytes() const noexcept
	{
		return max_total_bytes_;
	}

	// Steps the last 'endstream' terminator scan of this parser took (0 before any). Diagnostics for tests: it
	// shows the fallback scan stays linear.
	[[nodiscard]] size_t last_stream_terminator_scan_steps() const noexcept
	{
		return last_terminator_scan_steps_;
	}

	void set_max_container_elements(size_t max_elements) noexcept;
	[[nodiscard]] size_t max_container_elements() const noexcept
	{
		return max_container_elements_;
	}

	// Parses a single direct PDF object (null, boolean, integer, real, literal
	// or hex string, name, array '[...]', dictionary '<<...>>', or 'N G R'
	// indirect reference via 3-token lookahead).
	[[nodiscard]] PdfResult<PdfValue> parse_direct_object() noexcept;

	// Parses a PDF array '[...]' starting at the opening '[' token.
	[[nodiscard]] PdfResult<PdfArray> parse_array() noexcept;

	// Parses a PDF dictionary '<<...>>' starting at the opening '<<' token,
	// omitting null-valued entries per ISO 32000-1 section 7.3.7.
	[[nodiscard]] PdfResult<PdfDict> parse_dictionary() noexcept;

	// Parses a 'stream ... endstream' byte slice using direct /Length, an
	// optional indirect /Length resolver callback, and a fallback scan for
	// 'endstream' when /Length is missing, out of bounds, or does not land on
	// 'endstream'.
	[[nodiscard]] PdfResult<std::span<const uint8_t>> parse_stream_slice(
		const PdfDict& stream_dict,
		const LengthResolver& length_resolver = {}) noexcept;

	// Parses an indirect object definition 'N G obj ... endobj' (including
	// 'stream ... endstream' when the body dictionary is followed by 'stream').
	// Validates that N > 0 (fitting in uint32_t) and 0 <= G <= 65535.
	[[nodiscard]] PdfResult<IndirectObject> parse_indirect_object(
		const LengthResolver& length_resolver = {}) noexcept;

private:
	// Counts n payload bytes against the current parse's byte budget; false with err set when it is spent.
	[[nodiscard]] bool charge_bytes(size_t n, uint64_t byte_offset, PdfError& err) noexcept;
	[[nodiscard]] bool parse_direct_object_impl(
		size_t depth,
		PdfValue& out,
		PdfError& err);
	[[nodiscard]] bool parse_array_body(
		size_t depth,
		uint64_t open_offset,
		PdfArray& out,
		PdfError& err);
	[[nodiscard]] bool parse_dictionary_body(
		size_t depth,
		uint64_t open_offset,
		PdfDict& out,
		PdfError& err);
	[[nodiscard]] PdfResult<std::span<const uint8_t>> parse_stream_slice_impl(
		const PdfDict& stream_dict,
		const LengthResolver& length_resolver,
		bool require_endobj_terminator);
	[[nodiscard]] PdfResult<IndirectObject> parse_indirect_object_impl(
		const LengthResolver& length_resolver);

	std::optional<Lexer> owned_lexer_{std::nullopt};
	Lexer* lexer_{nullptr};
	size_t max_recursion_depth_{kMaxParserRecursionDepth};
	size_t max_container_elements_{kMaxContainerElements};
	size_t max_total_values_{kMaxParsedValues};
	size_t values_parsed_{0};               // values created by the current top-level parse
	size_t max_total_bytes_{kMaxParsedBytes};
	size_t bytes_parsed_{0};                // string, name and key bytes held by the current top-level parse
	size_t last_terminator_scan_steps_{0};
};


} // namespace recto::pdf
