// In-memory PDF object representation (ISO 32000-1:2008 section 7.3,
// ISO 32000-2:2020 section 7.3) for recto-pdf (D01 T01 section 2).
//
// Freeze contract (foundation plan section 4.2):
// Every PdfValue owns all of its storage by value (std::vector, std::string)
// with zero shared mutable pointers between Document instances, so distinct
// pdf::Document objects on separate DocumentActor threads share zero mutable
// state.
#pragma once

#include "error.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace recto::pdf {

inline constexpr size_t kMaxObjectCloneDepth = 64;

struct PdfNull {
	constexpr bool operator==(const PdfNull&) const noexcept = default;
};

struct PdfName {
	std::string value;

	PdfName() noexcept = default;

	explicit PdfName(std::string_view raw)
		: value(normalize(raw))
	{
	}

	explicit PdfName(const char* raw)
		: value(normalize(raw ? std::string_view(raw) : std::string_view{}))
	{
	}

	// Constructs a PdfName from an already-decoded name byte sequence without
	// stripping a leading '/' (e.g. a name parsed from "/#2FA" whose decoded
	// byte sequence begins with '/').
	[[nodiscard]] static PdfName from_decoded(std::string_view decoded_bytes)
	{
		PdfName name;
		name.value.assign(decoded_bytes.data(), decoded_bytes.size());
		return name;
	}

	[[nodiscard]] static std::string normalize(std::string_view raw)
	{
		if (!raw.empty() && raw.front() == '/') {
			raw.remove_prefix(1);
		}
		return std::string(raw);
	}

	[[nodiscard]] std::string_view view() const noexcept
	{
		return value;
	}

	[[nodiscard]] bool empty() const noexcept
	{
		return value.empty();
	}

	bool operator==(const PdfName& other) const noexcept = default;

	bool operator==(std::string_view other) const noexcept
	{
		if (value == other) {
			return true;
		}
		if (!other.empty() && other.front() == '/') {
			other.remove_prefix(1);
			return value == other;
		}
		return false;
	}
};

struct PdfString {
	std::vector<uint8_t> bytes;
	bool is_hex{false};

	PdfString() noexcept = default;

	explicit PdfString(std::vector<uint8_t> raw_bytes, bool hex = false) noexcept
		: bytes(std::move(raw_bytes))
		, is_hex(hex)
	{
	}

	explicit PdfString(std::span<const uint8_t> raw_bytes, bool hex = false)
		: bytes(raw_bytes.begin(), raw_bytes.end())
		, is_hex(hex)
	{
	}

	explicit PdfString(std::string_view ascii_bytes, bool hex = false)
		: bytes(
			reinterpret_cast<const uint8_t*>(ascii_bytes.data()),
			reinterpret_cast<const uint8_t*>(ascii_bytes.data()) + ascii_bytes.size())
		, is_hex(hex)
	{
	}

	[[nodiscard]] std::span<const uint8_t> span() const noexcept
	{
		return bytes;
	}

	[[nodiscard]] std::string_view as_byte_view() const noexcept
	{
		return std::string_view(
			reinterpret_cast<const char*>(bytes.data()),
			bytes.size());
	}

	[[nodiscard]] size_t size() const noexcept
	{
		return bytes.size();
	}

	[[nodiscard]] bool empty() const noexcept
	{
		return bytes.empty();
	}

	[[nodiscard]] PdfResult<std::string> to_utf8() const noexcept;

	bool operator==(const PdfString& other) const noexcept
	{
		return bytes == other.bytes;
	}
};

struct IndirectRef {
	uint32_t obj_num{0};
	uint16_t gen_num{0};

	constexpr IndirectRef() noexcept = default;
	constexpr IndirectRef(uint32_t obj, uint16_t gen = 0) noexcept
		: obj_num(obj)
		, gen_num(gen)
	{
	}

	constexpr bool operator==(const IndirectRef&) const noexcept = default;
};

class PdfValue;
class PdfDict;
struct PdfStream;

using PdfArray = std::vector<PdfValue>;

struct PdfRect {
	double x_min{0.0};
	double y_min{0.0};
	double x_max{0.0};
	double y_max{0.0};

	constexpr PdfRect() noexcept = default;

	constexpr PdfRect(double x1, double y1, double x2, double y2) noexcept
		: x_min(x1 <= x2 ? x1 : x2)
		, y_min(y1 <= y2 ? y1 : y2)
		, x_max(x1 <= x2 ? x2 : x1)
		, y_max(y1 <= y2 ? y2 : y1)
	{
	}

	[[nodiscard]] constexpr double width() const noexcept
	{
		return x_max >= x_min ? (x_max - x_min) : 0.0;
	}

	[[nodiscard]] constexpr double height() const noexcept
	{
		return y_max >= y_min ? (y_max - y_min) : 0.0;
	}

	[[nodiscard]] constexpr bool is_empty() const noexcept
	{
		return width() <= 0.0 || height() <= 0.0;
	}

	[[nodiscard]] static PdfResult<PdfRect> from_array(const PdfArray& arr) noexcept;

	constexpr bool operator==(const PdfRect&) const noexcept = default;
};

struct PdfMatrix {
	double a{1.0};
	double b{0.0};
	double c{0.0};
	double d{1.0};
	double e{0.0};
	double f{0.0};

	constexpr PdfMatrix() noexcept = default;

	constexpr PdfMatrix(
		double m_a,
		double m_b,
		double m_c,
		double m_d,
		double m_e,
		double m_f) noexcept
		: a(m_a)
		, b(m_b)
		, c(m_c)
		, d(m_d)
		, e(m_e)
		, f(m_f)
	{
	}

	[[nodiscard]] static constexpr PdfMatrix identity() noexcept
	{
		return PdfMatrix{};
	}

	[[nodiscard]] static PdfResult<PdfMatrix> from_array(const PdfArray& arr) noexcept;

	constexpr bool operator==(const PdfMatrix&) const noexcept = default;
};

class PdfDict {
public:
	using Entry = std::pair<PdfName, PdfValue>;
	using Container = std::vector<Entry>;
	using iterator = Container::iterator;
	using const_iterator = Container::const_iterator;

	PdfDict() noexcept = default;
	~PdfDict() noexcept;

	PdfDict(const PdfDict& other);
	PdfDict(PdfDict&& other) noexcept = default;
	PdfDict& operator=(const PdfDict& other);
	PdfDict& operator=(PdfDict&& other) noexcept = default;

	[[nodiscard]] size_t size() const noexcept { return entries_.size(); }
	[[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
	void reserve(size_t count) { entries_.reserve(count); }
	void clear() noexcept { entries_.clear(); }

	[[nodiscard]] iterator begin() noexcept { return entries_.begin(); }
	[[nodiscard]] iterator end() noexcept { return entries_.end(); }
	[[nodiscard]] const_iterator begin() const noexcept { return entries_.begin(); }
	[[nodiscard]] const_iterator end() const noexcept { return entries_.end(); }
	[[nodiscard]] const_iterator cbegin() const noexcept { return entries_.cbegin(); }
	[[nodiscard]] const_iterator cend() const noexcept { return entries_.cend(); }

	[[nodiscard]] const Container& entries() const noexcept { return entries_; }

	void set(PdfName key, PdfValue value);
	void set(std::string_view key, PdfValue value);
	bool erase(const PdfName& key) noexcept;
	bool erase(std::string_view key) noexcept;

	[[nodiscard]] bool contains(const PdfName& key) const noexcept;
	[[nodiscard]] bool contains(std::string_view key) const noexcept;
	[[nodiscard]] const PdfValue* get(const PdfName& key) const noexcept;
	[[nodiscard]] const PdfValue* get(std::string_view key) const noexcept;
	[[nodiscard]] PdfValue* get_mut(const PdfName& key) noexcept;
	[[nodiscard]] PdfValue* get_mut(std::string_view key) noexcept;

	[[nodiscard]] std::optional<bool> get_bool(std::string_view key) const noexcept;
	[[nodiscard]] std::optional<int64_t> get_int(std::string_view key) const noexcept;
	[[nodiscard]] std::optional<double> get_real(std::string_view key) const noexcept;
	[[nodiscard]] std::optional<double> get_number(std::string_view key) const noexcept;
	[[nodiscard]] std::optional<std::string_view> get_name(std::string_view key) const noexcept;
	[[nodiscard]] const PdfString* get_string(std::string_view key) const noexcept;
	[[nodiscard]] const PdfArray* get_array(std::string_view key) const noexcept;
	[[nodiscard]] PdfArray* get_array_mut(std::string_view key) noexcept;
	[[nodiscard]] const PdfDict* get_dict(std::string_view key) const noexcept;
	[[nodiscard]] PdfDict* get_dict_mut(std::string_view key) noexcept;
	[[nodiscard]] const PdfStream* get_stream(std::string_view key) const noexcept;
	[[nodiscard]] std::optional<IndirectRef> get_ref(std::string_view key) const noexcept;
	[[nodiscard]] std::optional<PdfRect> get_rect(std::string_view key) const noexcept;
	[[nodiscard]] std::optional<PdfMatrix> get_matrix(std::string_view key) const noexcept;

	bool operator==(const PdfDict& other) const noexcept;

private:
	Container entries_;
};

struct PdfStream {
	PdfDict dict;
	std::vector<uint8_t> raw_bytes;
	std::optional<std::vector<uint8_t>> decoded_bytes{std::nullopt};
	uint64_t file_offset{0};
	uint64_t raw_length{0};

	PdfStream() noexcept = default;

	PdfStream(PdfDict stream_dict, std::vector<uint8_t> raw) noexcept
		: dict(std::move(stream_dict))
		, raw_bytes(std::move(raw))
		, raw_length(raw_bytes.size())
	{
	}

	[[nodiscard]] std::span<const uint8_t> payload_bytes() const noexcept
	{
		if (decoded_bytes.has_value()) {
			return *decoded_bytes;
		}
		return raw_bytes;
	}

	bool operator==(const PdfStream& other) const noexcept
	{
		return dict == other.dict &&
			raw_bytes == other.raw_bytes &&
			decoded_bytes == other.decoded_bytes;
	}
};

enum class PdfValueKind : uint8_t {
	Null = 0,
	Bool = 1,
	Int = 2,
	Real = 3,
	Name = 4,
	String = 5,
	Array = 6,
	Dict = 7,
	Stream = 8,
	Ref = 9,
};

class PdfValue {
public:
	using Storage = std::variant<
		PdfNull,
		bool,
		int64_t,
		double,
		PdfName,
		PdfString,
		PdfArray,
		PdfDict,
		PdfStream,
		IndirectRef>;

	PdfValue() noexcept
		: storage_(PdfNull{})
	{
	}

	PdfValue(std::nullptr_t) noexcept
		: storage_(PdfNull{})
	{
	}

	PdfValue(PdfNull) noexcept
		: storage_(PdfNull{})
	{
	}

	PdfValue(bool v) noexcept
		: storage_(v)
	{
	}

	PdfValue(int32_t v) noexcept
		: storage_(static_cast<int64_t>(v))
	{
	}

	PdfValue(int64_t v) noexcept
		: storage_(v)
	{
	}

	PdfValue(double v) noexcept
		: storage_(v)
	{
	}

	PdfValue(PdfName v) noexcept
		: storage_(std::move(v))
	{
	}

	PdfValue(PdfString v) noexcept
		: storage_(std::move(v))
	{
	}

	PdfValue(PdfArray v) noexcept
		: storage_(std::move(v))
	{
	}

	PdfValue(PdfDict v) noexcept
		: storage_(std::move(v))
	{
	}

	PdfValue(PdfStream v) noexcept
		: storage_(std::move(v))
	{
	}

	PdfValue(IndirectRef v) noexcept
		: storage_(v)
	{
	}

	PdfValue(const PdfValue&) = default;
	PdfValue(PdfValue&&) noexcept = default;
	PdfValue& operator=(const PdfValue&) = default;
	PdfValue& operator=(PdfValue&&) noexcept = default;
	~PdfValue() noexcept = default;

	[[nodiscard]] PdfValueKind kind() const noexcept
	{
		return static_cast<PdfValueKind>(storage_.index());
	}

	[[nodiscard]] bool is_null() const noexcept { return std::holds_alternative<PdfNull>(storage_); }
	[[nodiscard]] bool is_bool() const noexcept { return std::holds_alternative<bool>(storage_); }
	[[nodiscard]] bool is_int() const noexcept { return std::holds_alternative<int64_t>(storage_); }
	[[nodiscard]] bool is_real() const noexcept { return std::holds_alternative<double>(storage_); }
	[[nodiscard]] bool is_number() const noexcept { return is_int() || is_real(); }
	[[nodiscard]] bool is_name() const noexcept { return std::holds_alternative<PdfName>(storage_); }
	[[nodiscard]] bool is_string() const noexcept { return std::holds_alternative<PdfString>(storage_); }
	[[nodiscard]] bool is_array() const noexcept { return std::holds_alternative<PdfArray>(storage_); }
	[[nodiscard]] bool is_dict() const noexcept { return std::holds_alternative<PdfDict>(storage_); }
	[[nodiscard]] bool is_stream() const noexcept { return std::holds_alternative<PdfStream>(storage_); }
	[[nodiscard]] bool is_ref() const noexcept { return std::holds_alternative<IndirectRef>(storage_); }

	[[nodiscard]] std::optional<bool> as_bool() const noexcept
	{
		if (const auto* p = std::get_if<bool>(&storage_)) {
			return *p;
		}
		return std::nullopt;
	}

	[[nodiscard]] std::optional<int64_t> as_int() const noexcept
	{
		if (const auto* p = std::get_if<int64_t>(&storage_)) {
			return *p;
		}
		return std::nullopt;
	}

	[[nodiscard]] std::optional<double> as_real() const noexcept
	{
		if (const auto* p = std::get_if<double>(&storage_)) {
			return *p;
		}
		return std::nullopt;
	}

	// Numeric promotion (ISO 32000-1 section 7.3.3): both integer and real PDF
	// tokens promote seamlessly to finite double for coordinate and matrix math.
	[[nodiscard]] std::optional<double> as_number() const noexcept
	{
		if (const auto* i = std::get_if<int64_t>(&storage_)) {
			return static_cast<double>(*i);
		}
		if (const auto* r = std::get_if<double>(&storage_)) {
			if (std::isfinite(*r)) {
				return *r;
			}
			return std::nullopt;
		}
		return std::nullopt;
	}

	[[nodiscard]] std::optional<std::string_view> as_name() const noexcept
	{
		if (const auto* p = std::get_if<PdfName>(&storage_)) {
			return p->view();
		}
		return std::nullopt;
	}

	[[nodiscard]] const PdfName* as_name_obj() const noexcept
	{
		return std::get_if<PdfName>(&storage_);
	}

	[[nodiscard]] const PdfString* as_string() const noexcept
	{
		return std::get_if<PdfString>(&storage_);
	}

	[[nodiscard]] PdfString* as_string_mut() noexcept
	{
		return std::get_if<PdfString>(&storage_);
	}

	[[nodiscard]] const PdfArray* as_array() const noexcept
	{
		return std::get_if<PdfArray>(&storage_);
	}

	[[nodiscard]] PdfArray* as_array_mut() noexcept
	{
		return std::get_if<PdfArray>(&storage_);
	}

	[[nodiscard]] const PdfDict* as_dict() const noexcept
	{
		return std::get_if<PdfDict>(&storage_);
	}

	[[nodiscard]] PdfDict* as_dict_mut() noexcept
	{
		return std::get_if<PdfDict>(&storage_);
	}

	[[nodiscard]] const PdfStream* as_stream() const noexcept
	{
		return std::get_if<PdfStream>(&storage_);
	}

	[[nodiscard]] PdfStream* as_stream_mut() noexcept
	{
		return std::get_if<PdfStream>(&storage_);
	}

	[[nodiscard]] std::optional<IndirectRef> as_ref() const noexcept
	{
		if (const auto* p = std::get_if<IndirectRef>(&storage_)) {
			return *p;
		}
		return std::nullopt;
	}

	[[nodiscard]] PdfResult<PdfRect> as_rect() const noexcept;
	[[nodiscard]] PdfResult<PdfMatrix> as_matrix() const noexcept;

	// Performs an owned deep clone bounded by min(max_depth, 64).
	// Exceeding the effective depth returns ErrorCode::RecursionLimitExceeded.
	[[nodiscard]] PdfResult<PdfValue> deep_clone(
		size_t max_depth = kMaxObjectCloneDepth) const noexcept;

	bool operator==(const PdfValue& other) const noexcept
	{
		return storage_ == other.storage_;
	}

private:
	Storage storage_;
};

// Decodes a PDF text string (ISO 32000-1 section 7.9.2.2 / Table D.2 and
// ISO 32000-2 section 7.9.2.2) into UTF-8:
// - UTF-16BE when prefixed with BOM 0xFE 0xFF (handling surrogate pairs and
//   stripping embedded language escape sequences 0x001B ... 0x001B).
// - UTF-8 when prefixed with BOM 0xEF 0xBB 0xBF (PDF 2.0, stripping embedded
//   language escape sequences 0x1B ... 0x1B).
// - PDFDocEncoding (ISO 32000-1 Table D.2) otherwise.
[[nodiscard]] PdfResult<std::string> pdf_string_to_utf8(std::span<const uint8_t> bytes) noexcept;

// Encodes a UTF-8 string into a PDF text string:
// - Uses PDFDocEncoding when every Unicode scalar value is representable in
//   PDFDocEncoding without colliding with the UTF-16BE / UTF-8 BOM prefixes.
// - Uses UTF-16BE with a leading 0xFE 0xFF BOM (and surrogate pairs for
//   U+10000..U+10FFFF) when any scalar value falls outside PDFDocEncoding.
[[nodiscard]] PdfResult<PdfString> utf8_to_pdf_string(std::string_view utf8) noexcept;

} // namespace recto::pdf
