// The PostScript-like tokens of a CMap stream (Adobe Technical Note 5014), shared by the ToUnicode CMaps of D01 T02
// section 28 and the CID CMaps of section 30. Internal to recto-pdf.
#pragma once

#include "error.h"
#include "font_encoding.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace recto::pdf {

struct CMapLexer {
	enum class Kind : uint8_t { End, Hex, Name, Keyword, ArrayOpen, ArrayClose, Other };
	struct Token {
		Kind kind = Kind::End;
		std::string_view text{};
		std::string hex{};
	};

	std::span<const uint8_t> in;
	size_t at = 0;
	size_t max_hex_bytes = kMaxToUnicodeTargetBytes; // a hex string's decoded bytes

	[[nodiscard]] static bool white(uint8_t c) noexcept
	{
		return c == 0u || c == 9u || c == 10u || c == 12u || c == 13u || c == 32u;
	}
	[[nodiscard]] static bool delimiter(uint8_t c) noexcept
	{
		return white(c) || c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' ||
			c == '}' || c == '/' || c == '%';
	}

	// The next token; a hex string's bytes are decoded, past `max_hex_bytes` it is `ResourceLimit`.
	[[nodiscard]] PdfResult<Token> next()
	{
		for (;;) {
			while (at < in.size() && white(in[at])) {
				++at;
			}
			if (at < in.size() && in[at] == '%') {
				while (at < in.size() && in[at] != '\n' && in[at] != '\r') {
					++at;
				}
				continue;
			}
			break;
		}
		Token token;
		if (at >= in.size()) {
			return token;
		}
		const uint8_t c = in[at];
		if (c == '<' && at + 1u < in.size() && in[at + 1u] == '<') {
			at += 2u;
			token.kind = Kind::Other;
			return token;
		}
		if (c == '<') {
			++at;
			int high = -1;
			token.kind = Kind::Hex;
			while (at < in.size() && in[at] != '>') {
				const uint8_t d = in[at++];
				if (white(d)) {
					continue;
				}
				int value = -1;
				if (d >= '0' && d <= '9') {
					value = d - '0';
				} else if (d >= 'a' && d <= 'f') {
					value = d - 'a' + 10;
				} else if (d >= 'A' && d <= 'F') {
					value = d - 'A' + 10;
				} else {
					return make_pdf_error(ErrorCode::DamagedFormat, "CMap hex string holds a non-hex byte");
				}
				if (high < 0) {
					high = value;
				} else {
					token.hex.push_back(static_cast<char>((high << 4) | value));
					high = -1;
					if (token.hex.size() > max_hex_bytes) {
						return make_pdf_error(ErrorCode::ResourceLimit, "CMap hex string exceeds its length limit");
					}
				}
			}
			if (at >= in.size()) {
				return make_pdf_error(ErrorCode::DamagedFormat, "CMap hex string is not closed");
			}
			++at;
			if (high >= 0) {
				token.hex.push_back(static_cast<char>(high << 4)); // an odd digit count: the last digit padded
			}
			return token;
		}
		if (c == '>' && at + 1u < in.size() && in[at + 1u] == '>') {
			at += 2u;
			token.kind = Kind::Other;
			return token;
		}
		if (c == '[' || c == ']') {
			++at;
			token.kind = c == '[' ? Kind::ArrayOpen : Kind::ArrayClose;
			return token;
		}
		if (c == '(') {
			// A literal string, skipped with its nesting and escapes.
			int depth = 0;
			do {
				if (in[at] == '\\') {
					++at;
				} else if (in[at] == '(') {
					++depth;
				} else if (in[at] == ')') {
					--depth;
				}
				++at;
			} while (at < in.size() && depth > 0);
			token.kind = Kind::Other;
			return token;
		}
		const size_t start = c == '/' ? at + 1u : at;
		at = start;
		while (at < in.size() && !delimiter(in[at])) {
			++at;
		}
		if (at == start && c != '/') {
			++at; // a stray delimiter ('>', ')', '{', '}')
			token.kind = Kind::Other;
			return token;
		}
		token.kind = c == '/' ? Kind::Name : Kind::Keyword;
		token.text = std::string_view(reinterpret_cast<const char*>(in.data() + start), at - start);
		return token;
	}
};

} // namespace recto::pdf
