// The canonical PdfValue serializer (ISO 32000-1 sections 7.3 and 7.5; D01 T02 section 35).
#include "serialize.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <new>
#include <optional>
#include <string_view>

namespace recto::pdf {

namespace {

thread_local size_t t_fail_after_bytes = std::numeric_limits<size_t>::max();

// ISO 32000-1 7.2.2: the delimiter characters, which a name escapes along with whitespace and `#`.
[[nodiscard]] constexpr bool is_delimiter(uint8_t b) noexcept
{
	return b == '(' || b == ')' || b == '<' || b == '>' || b == '[' || b == ']' || b == '{' || b == '}' || b == '/' ||
		b == '%';
}

constexpr std::string_view kHexDigits = "0123456789ABCDEF";

// Appends to `out` within the call's budget; the first failure is kept, and every later append does nothing.
class Writer {
public:
	Writer(std::string& out, size_t max_bytes) noexcept
		: out_(out)
		, start_(out.size())
		, max_(max_bytes)
	{
	}

	void put(std::string_view s)
	{
		if (failed()) {
			return;
		}
		const size_t written = out_.size() - start_;
		if (s.size() > max_ - std::min(max_, written)) {
			error_ = make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::WorkingBytes, written + s.size(), max_,
				"Serialized object is larger than its output limit");
			return;
		}
		if (written + s.size() > t_fail_after_bytes) {
			throw std::bad_alloc();
		}
		out_.append(s);
	}

	void put(char c)
	{
		put(std::string_view(&c, 1u));
	}

	void fail(PdfError error) noexcept
	{
		if (!failed()) {
			error_ = std::move(error);
		}
	}

	[[nodiscard]] bool failed() const noexcept
	{
		return error_.has_value();
	}

	// The call's result: on failure `out` is cut back to where it started.
	[[nodiscard]] PdfResult<void> finish() noexcept
	{
		if (!failed()) {
			return {};
		}
		rollback();
		return *error_;
	}

	void rollback() noexcept
	{
		out_.resize(start_);
	}

private:
	std::string& out_;
	size_t start_;
	size_t max_;
	std::optional<PdfError> error_{};
};

void write_integer(Writer& w, int64_t v)
{
	std::array<char, 24> buf{};
	const auto [end, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v);
	static_cast<void>(ec); // 24 characters hold every int64_t
	w.put(std::string_view(buf.data(), static_cast<size_t>(end - buf.data())));
}

// The shortest fixed-notation digits that read back as exactly `v` (ISO 32000-1 7.3.3 has no exponent form), always
// with a decimal point so the value reads back as a real, not an integer.
void write_real(Writer& w, double v)
{
	if (!std::isfinite(v)) {
		w.fail(make_pdf_error(ErrorCode::InvalidArgument, "A real number that is not finite cannot be written"));
		return;
	}
	std::array<char, 400> buf{}; // the longest fixed form of a double is 327 characters (the smallest subnormal)
	const auto [end, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::fixed);
	if (ec != std::errc{}) {
		w.fail(make_pdf_error(ErrorCode::InternalError, "A real number did not fit its formatting buffer"));
		return;
	}
	const std::string_view digits(buf.data(), static_cast<size_t>(end - buf.data()));
	w.put(digits);
	if (digits.find('.') == std::string_view::npos) {
		w.put(".0");
	}
}

void write_name(Writer& w, std::string_view name)
{
	w.put('/');
	for (const char c : name) {
		const auto b = static_cast<uint8_t>(c);
		if (b < 0x21u || b > 0x7Eu || b == '#' || is_delimiter(b)) {
			const std::array<char, 3> esc = {'#', kHexDigits[b >> 4u], kHexDigits[b & 0xFu]};
			w.put(std::string_view(esc.data(), esc.size()));
		} else {
			w.put(c);
		}
	}
}

// A literal string with every delimiter and control byte escaped, or a hex string when it was written as one or
// more than a quarter of its bytes would need escaping.
void write_string(Writer& w, const PdfString& s)
{
	const auto plain = [](uint8_t b) { return b >= 0x20u && b <= 0x7Eu; };
	size_t binary = 0;
	for (const uint8_t b : s.bytes) {
		binary += plain(b) ? 0u : 1u;
	}
	if (s.is_hex || binary * 4u > s.bytes.size()) {
		w.put('<');
		for (const uint8_t b : s.bytes) {
			const std::array<char, 2> hex = {kHexDigits[b >> 4u], kHexDigits[b & 0xFu]};
			w.put(std::string_view(hex.data(), hex.size()));
		}
		w.put('>');
		return;
	}
	w.put('(');
	for (const uint8_t b : s.bytes) {
		switch (b) {
		case '(':
			w.put("\\(");
			break;
		case ')':
			w.put("\\)");
			break;
		case '\\':
			w.put("\\\\");
			break;
		case '\n':
			w.put("\\n");
			break;
		case '\r':
			w.put("\\r"); // a bare CR would read back as LF (ISO 32000-1 7.3.4.2)
			break;
		case '\t':
			w.put("\\t");
			break;
		case '\b':
			w.put("\\b");
			break;
		case '\f':
			w.put("\\f");
			break;
		default:
			if (plain(b)) {
				w.put(static_cast<char>(b));
			} else {
				const std::array<char, 4> oct = {'\\', static_cast<char>('0' + (b >> 6u)), static_cast<char>('0' + ((b >> 3u) & 7u)),
					static_cast<char>('0' + (b & 7u))};
				w.put(std::string_view(oct.data(), oct.size()));
			}
		}
	}
	w.put(')');
}

void write_ref(Writer& w, IndirectRef ref)
{
	write_integer(w, ref.obj_num);
	w.put(' ');
	write_integer(w, ref.gen_num);
	w.put(" R");
}

void write_value(Writer& w, const PdfValue& v, size_t depth);

// A dictionary's entries; `skip_length` leaves out `/Length` (a stream writes its own).
void write_dict(Writer& w, const PdfDict& d, size_t depth, bool skip_length)
{
	w.put("<<");
	bool first = true;
	for (const auto& [key, value] : d) {
		if (skip_length && key == "Length") {
			continue;
		}
		if (!first) {
			w.put(' ');
		}
		first = false;
		write_name(w, key.view());
		w.put(' ');
		write_value(w, value, depth + 1u);
		if (w.failed()) {
			return;
		}
	}
	if (!skip_length) {
		w.put(">>");
	}
}

void write_value(Writer& w, const PdfValue& v, size_t depth)
{
	if (w.failed()) {
		return;
	}
	if ((v.is_array() || v.is_dict()) && depth >= kMaxSerializedDepth) {
		w.fail(make_pdf_error(ErrorCode::RecursionLimitExceeded, "Value nests deeper than a PDF object may"));
		return;
	}
	if (v.is_null()) {
		w.put("null");
	} else if (const auto b = v.as_bool()) {
		w.put(*b ? "true" : "false");
	} else if (const auto i = v.as_int()) {
		write_integer(w, *i);
	} else if (const auto r = v.as_real()) {
		write_real(w, *r);
	} else if (const PdfName* n = v.as_name_obj()) {
		write_name(w, n->view());
	} else if (const PdfString* s = v.as_string()) {
		write_string(w, *s);
	} else if (const PdfArray* a = v.as_array()) {
		w.put('[');
		for (size_t k = 0; k < a->size() && !w.failed(); ++k) {
			if (k != 0u) {
				w.put(' ');
			}
			write_value(w, (*a)[k], depth + 1u);
		}
		w.put(']');
	} else if (const PdfDict* d = v.as_dict()) {
		write_dict(w, *d, depth, false);
	} else if (const auto ref = v.as_ref()) {
		write_ref(w, *ref);
	} else {
		w.fail(make_pdf_error(ErrorCode::InvalidArgument, "A stream is an indirect object, not a direct value"));
	}
}

} // namespace

namespace detail {

ScopedSerializeAllocFailure::ScopedSerializeAllocFailure(size_t after_bytes) noexcept
	: previous_(t_fail_after_bytes)
{
	t_fail_after_bytes = after_bytes;
}

ScopedSerializeAllocFailure::~ScopedSerializeAllocFailure() noexcept
{
	t_fail_after_bytes = previous_;
}

} // namespace detail

PdfResult<void> serialize_value(const PdfValue& value, std::string& out, size_t max_bytes) noexcept
{
	Writer w(out, max_bytes);
	try {
		write_value(w, value, 0u);
		return w.finish();
	} catch (const std::bad_alloc&) {
		w.rollback();
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory serializing a PDF value");
	}
}

PdfResult<void> serialize_indirect(IndirectRef ref, const PdfValue& value, std::string& out, size_t max_bytes) noexcept
{
	Writer w(out, max_bytes);
	try {
		write_integer(w, ref.obj_num);
		w.put(' ');
		write_integer(w, ref.gen_num);
		w.put(" obj\n");
		if (const PdfStream* stream = value.as_stream()) {
			// The encoded bytes, which the dictionary's filters describe; the decoded cache is never written.
			write_dict(w, stream->dict, 0u, true);
			w.put(stream->dict.empty() || (stream->dict.size() == 1u && stream->dict.contains("Length")) ? "/Length " : " /Length ");
			write_integer(w, static_cast<int64_t>(stream->raw_bytes.size()));
			w.put(">>\nstream\n");
			w.put(std::string_view(reinterpret_cast<const char*>(stream->raw_bytes.data()), stream->raw_bytes.size()));
			w.put("\nendstream");
		} else {
			write_value(w, value, 0u);
		}
		w.put("\nendobj\n");
		return w.finish();
	} catch (const std::bad_alloc&) {
		w.rollback();
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory serializing an indirect object");
	}
}

} // namespace recto::pdf
