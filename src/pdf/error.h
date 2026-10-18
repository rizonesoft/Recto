// Recto PDF engine error-handling and D16 diagnostic primitives (D01 T01 section 1).
//
// Defines ErrorCode, PdfError, PdfResult<T>, PdfResult<void>, the 256-slot
// thread-safe in-memory diagnostic ring buffer, and Windows Event Tracing
// (ETW TraceLogging) span helpers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace recto::pdf {

enum class ErrorCode : uint32_t {
	Ok = 0,
	FileNotFound,
	AccessDenied,
	NotAPdf,
	DamagedFormat,
	NeedsPassword,
	WrongPassword,
	UnsupportedEncryption,
	UnsupportedFeature,
	DecompressionLimitExceeded,
	RecursionLimitExceeded,
	SignatureInvalid,
	ComplianceFailed,
	OutOfMemory,
	IoError,
	Cancelled,
	InternalError,
};

[[nodiscard]] constexpr std::string_view error_code_name(ErrorCode code) noexcept
{
	switch (code) {
	case ErrorCode::Ok:
		return "Ok";
	case ErrorCode::FileNotFound:
		return "FileNotFound";
	case ErrorCode::AccessDenied:
		return "AccessDenied";
	case ErrorCode::NotAPdf:
		return "NotAPdf";
	case ErrorCode::DamagedFormat:
		return "DamagedFormat";
	case ErrorCode::NeedsPassword:
		return "NeedsPassword";
	case ErrorCode::WrongPassword:
		return "WrongPassword";
	case ErrorCode::UnsupportedEncryption:
		return "UnsupportedEncryption";
	case ErrorCode::UnsupportedFeature:
		return "UnsupportedFeature";
	case ErrorCode::DecompressionLimitExceeded:
		return "DecompressionLimitExceeded";
	case ErrorCode::RecursionLimitExceeded:
		return "RecursionLimitExceeded";
	case ErrorCode::SignatureInvalid:
		return "SignatureInvalid";
	case ErrorCode::ComplianceFailed:
		return "ComplianceFailed";
	case ErrorCode::OutOfMemory:
		return "OutOfMemory";
	case ErrorCode::IoError:
		return "IoError";
	case ErrorCode::Cancelled:
		return "Cancelled";
	case ErrorCode::InternalError:
		return "InternalError";
	}
	return "UnknownError";
}

[[nodiscard]] constexpr bool should_auto_record_diagnostic(ErrorCode code) noexcept
{
	switch (code) {
	case ErrorCode::DamagedFormat:
	case ErrorCode::DecompressionLimitExceeded:
	case ErrorCode::RecursionLimitExceeded:
		return true;
	default:
		return false;
	}
}

struct PdfError {
	ErrorCode code{ErrorCode::Ok};
	std::string message;
	std::optional<uint64_t> byte_offset{std::nullopt};
	std::optional<uint32_t> object_num{std::nullopt};
	uint16_t generation_num{0};

	[[nodiscard]] std::string format() const;

	bool operator==(const PdfError& other) const noexcept = default;
};

namespace detail {

[[noreturn]] void fail_fast_invalid_result_access() noexcept;

} // namespace detail

template <typename T>
class [[nodiscard]] PdfResult {
	static_assert(
		!std::is_same_v<std::remove_cvref_t<T>, PdfError>,
		"PdfResult<T> value type cannot be PdfError");

public:
	using value_type = T;
	using error_type = PdfError;

	PdfResult(const T& value)
		requires std::is_copy_constructible_v<T>
		: storage_(std::in_place_index<0>, value)
	{
	}

	PdfResult(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
		requires std::is_move_constructible_v<T>
		: storage_(std::in_place_index<0>, std::move(value))
	{
	}

	PdfResult(const PdfError& error)
		: storage_(std::in_place_index<1>, error)
	{
	}

	PdfResult(PdfError&& error) noexcept
		: storage_(std::in_place_index<1>, std::move(error))
	{
	}

	PdfResult(const PdfResult&) = default;
	PdfResult& operator=(const PdfResult&) = default;
	PdfResult(PdfResult&&) noexcept(
		std::is_nothrow_move_constructible_v<T> &&
		std::is_nothrow_move_constructible_v<PdfError>) = default;
	PdfResult& operator=(PdfResult&&) noexcept(
		std::is_nothrow_move_assignable_v<T> &&
		std::is_nothrow_move_assignable_v<PdfError>) = default;

	[[nodiscard]] bool has_value() const noexcept
	{
		return storage_.index() == 0;
	}

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return has_value();
	}

	[[nodiscard]] T& value() & noexcept
	{
		auto* ptr = std::get_if<0>(&storage_);
		if (!ptr) {
			detail::fail_fast_invalid_result_access();
		}
		return *ptr;
	}

	[[nodiscard]] const T& value() const& noexcept
	{
		const auto* ptr = std::get_if<0>(&storage_);
		if (!ptr) {
			detail::fail_fast_invalid_result_access();
		}
		return *ptr;
	}

	[[nodiscard]] T&& value() && noexcept
	{
		auto* ptr = std::get_if<0>(&storage_);
		if (!ptr) {
			detail::fail_fast_invalid_result_access();
		}
		return std::move(*ptr);
	}

	[[nodiscard]] const T&& value() const&& noexcept
	{
		const auto* ptr = std::get_if<0>(&storage_);
		if (!ptr) {
			detail::fail_fast_invalid_result_access();
		}
		return std::move(*ptr);
	}

	[[nodiscard]] T& operator*() & noexcept { return value(); }
	[[nodiscard]] const T& operator*() const& noexcept { return value(); }
	[[nodiscard]] T&& operator*() && noexcept { return std::move(*this).value(); }
	[[nodiscard]] const T&& operator*() const&& noexcept { return std::move(*this).value(); }

	[[nodiscard]] T* operator->() noexcept { return &value(); }
	[[nodiscard]] const T* operator->() const noexcept { return &value(); }

	[[nodiscard]] PdfError& error() & noexcept
	{
		auto* ptr = std::get_if<1>(&storage_);
		if (!ptr) {
			detail::fail_fast_invalid_result_access();
		}
		return *ptr;
	}

	[[nodiscard]] const PdfError& error() const& noexcept
	{
		const auto* ptr = std::get_if<1>(&storage_);
		if (!ptr) {
			detail::fail_fast_invalid_result_access();
		}
		return *ptr;
	}

	[[nodiscard]] PdfError&& error() && noexcept
	{
		auto* ptr = std::get_if<1>(&storage_);
		if (!ptr) {
			detail::fail_fast_invalid_result_access();
		}
		return std::move(*ptr);
	}

	[[nodiscard]] const PdfError&& error() const&& noexcept
	{
		const auto* ptr = std::get_if<1>(&storage_);
		if (!ptr) {
			detail::fail_fast_invalid_result_access();
		}
		return std::move(*ptr);
	}

	template <typename U>
	[[nodiscard]] T value_or(U&& default_value) const&
		requires std::is_copy_constructible_v<T> && std::is_convertible_v<U&&, T>
	{
		if (has_value()) {
			return value();
		}
		return static_cast<T>(std::forward<U>(default_value));
	}

	template <typename U>
	[[nodiscard]] T value_or(U&& default_value) &&
		requires std::is_move_constructible_v<T> && std::is_convertible_v<U&&, T>
	{
		if (has_value()) {
			return std::move(*this).value();
		}
		return static_cast<T>(std::forward<U>(default_value));
	}

private:
	std::variant<T, PdfError> storage_;
};

template <>
class [[nodiscard]] PdfResult<void> {
public:
	using value_type = void;
	using error_type = PdfError;

	PdfResult() noexcept = default;

	PdfResult(const PdfError& error)
		: error_(error)
	{
	}

	PdfResult(PdfError&& error) noexcept
		: error_(std::move(error))
	{
	}

	[[nodiscard]] static PdfResult ok() noexcept
	{
		return PdfResult();
	}

	[[nodiscard]] bool has_value() const noexcept
	{
		return !error_.has_value();
	}

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return has_value();
	}

	void value() const noexcept
	{
		if (error_.has_value()) {
			detail::fail_fast_invalid_result_access();
		}
	}

	void operator*() const noexcept
	{
		value();
	}

	[[nodiscard]] PdfError& error() & noexcept
	{
		if (!error_.has_value()) {
			detail::fail_fast_invalid_result_access();
		}
		return *error_;
	}

	[[nodiscard]] const PdfError& error() const& noexcept
	{
		if (!error_.has_value()) {
			detail::fail_fast_invalid_result_access();
		}
		return *error_;
	}

	[[nodiscard]] PdfError&& error() && noexcept
	{
		if (!error_.has_value()) {
			detail::fail_fast_invalid_result_access();
		}
		return std::move(*error_);
	}

	[[nodiscard]] const PdfError&& error() const&& noexcept
	{
		if (!error_.has_value()) {
			detail::fail_fast_invalid_result_access();
		}
		return std::move(*error_);
	}

private:
	std::optional<PdfError> error_{std::nullopt};
};

inline constexpr size_t kDiagnosticRingCapacity = 256;
inline constexpr size_t kMaxDiagnosticMessageBytes = 255;

struct DiagnosticEntry {
	uint64_t sequence{0};
	uint64_t timestamp_utc_ms{0};
	uint32_t thread_id{0};
	ErrorCode code{ErrorCode::Ok};
	std::string message;
	std::optional<uint64_t> byte_offset{std::nullopt};
	std::optional<uint32_t> object_num{std::nullopt};
	uint16_t generation_num{0};

	[[nodiscard]] std::string format() const;
};

// Records an engine warning or error in the fixed-capacity 256-slot diagnostic
// ring buffer without heap allocation inside the ring and emits a structured
// ETW TraceLogging event containing only error code, offsets, object ID, and
// sequence metadata (zero document byte payloads).
void record_diagnostic(
	ErrorCode code,
	std::string_view message,
	std::optional<uint64_t> byte_offset = std::nullopt,
	std::optional<uint32_t> object_num = std::nullopt,
	uint16_t generation_num = 0) noexcept;

void record_diagnostic(const PdfError& err) noexcept;

// Returns up to max_entries of the newest diagnostics in chronological order
// (oldest to newest among the returned entries).
[[nodiscard]] std::vector<DiagnosticEntry> diagnostics_tail(
	size_t max_entries = kDiagnosticRingCapacity);

// Clears the in-memory diagnostic ring buffer and resets sequence numbering.
void clear_diagnostics() noexcept;

// Returns the total number of diagnostics recorded since startup or the last
// clear_diagnostics() call.
[[nodiscard]] uint64_t diagnostics_total_recorded() noexcept;

// Constructs a PdfError and automatically appends a diagnostic ring entry and
// emits a TraceLoggingWrite event when code is DamagedFormat,
// DecompressionLimitExceeded, or RecursionLimitExceeded.
[[nodiscard]] PdfError make_pdf_error(
	ErrorCode code,
	std::string message,
	std::optional<uint64_t> byte_offset = std::nullopt,
	std::optional<uint32_t> object_num = std::nullopt,
	uint16_t generation_num = 0);

// Ensures the "Recto.Diagnostics" ETW TraceLogging provider is registered.
bool ensure_etw_provider_registered() noexcept;

[[nodiscard]] bool is_etw_provider_registered() noexcept;
[[nodiscard]] std::string_view etw_provider_name() noexcept;
[[nodiscard]] uint64_t etw_diagnostic_events_emitted() noexcept;
[[nodiscard]] uint64_t etw_span_events_emitted() noexcept;

// RAII scoped ETW TraceLogging span for open, render, and job phases (D16).
// Emits SpanStart on construction and SpanEnd with elapsed microseconds on
// destruction (containing only phase name, duration, document/page indices,
// and thread ID; zero document byte payloads).
class EtwSpan {
public:
	explicit EtwSpan(
		const char* phase_name,
		uint64_t document_id = 0,
		uint32_t page_index = 0) noexcept;
	~EtwSpan() noexcept;

	EtwSpan(const EtwSpan&) = delete;
	EtwSpan& operator=(const EtwSpan&) = delete;
	EtwSpan(EtwSpan&&) = delete;
	EtwSpan& operator=(EtwSpan&&) = delete;

	[[nodiscard]] uint64_t elapsed_us() const noexcept;

private:
	const char* phase_name_{""};
	uint64_t document_id_{0};
	uint32_t page_index_{0};
	uint32_t thread_id_{0};
	int64_t start_qpc_{0};
};

} // namespace recto::pdf

#define RECTO_PDF_CONCAT_INNER(a, b) a##b
#define RECTO_PDF_CONCAT(a, b) RECTO_PDF_CONCAT_INNER(a, b)

#define RECTO_PDF_RETURN_IF_ERROR(expr)                                                  \
	do {                                                                                 \
		auto&& _recto_pdf_res = (expr);                                                  \
		if (!_recto_pdf_res.has_value()) {                                               \
			return std::move(_recto_pdf_res).error();                                    \
		}                                                                                \
	} while (0)

#define RECTO_PDF_TRY_IMPL(tmp_var, var_decl, expr)                                      \
	auto&& tmp_var = (expr);                                                             \
	if (!tmp_var.has_value()) {                                                          \
		return std::move(tmp_var).error();                                               \
	}                                                                                    \
	var_decl = std::move(tmp_var).value()

#define RECTO_PDF_TRY(var_decl, expr)                                                    \
	RECTO_PDF_TRY_IMPL(RECTO_PDF_CONCAT(_recto_pdf_try_, __LINE__), var_decl, (expr))

#define RECTO_ETW_SPAN(phase_name)                                                       \
	::recto::pdf::EtwSpan RECTO_PDF_CONCAT(_recto_etw_span_, __LINE__)((phase_name))

#define RECTO_ETW_SPAN_DOC(phase_name, doc_id, page_index)                               \
	::recto::pdf::EtwSpan RECTO_PDF_CONCAT(_recto_etw_span_, __LINE__)(                  \
		(phase_name), (doc_id), (page_index))
