// Recto PDF engine error-handling and D16 diagnostic primitives (D01 T01 section 1).
//
// Defines ErrorCode, DiagnosticId, EtwPhaseId, PdfError, PdfResult<T>,
// PdfResult<void>, the 256-slot thread-safe in-memory diagnostic ring buffer,
// and Windows Event Tracing (ETW TraceLogging) span helpers.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <new>
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

// Closed catalog of structured diagnostic identifiers stored in the D16 ring
// buffer. Only DiagnosticId enumerators and numeric offsets/object IDs are
// retained in the ring buffer, guaranteeing zero document byte payloads.
enum class DiagnosticId : uint16_t {
	GenericStatus = 0,
	FileNotFound,
	AccessDenied,
	NotAPdfHeader,
	DamagedFormat,
	DamagedHeader,
	DamagedToken,
	DamagedObject,
	DamagedDictionary,
	DamagedXRef,
	DamagedStream,
	DamagedTrailer,
	RecoveredViaLinearScan,
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
	BlockedLaunchAction,
	BlockedJavaScriptAction,
	BlockedSubmitFormAction,
	BlockedImportDataAction,
	CorruptRecentsStore,
	CorruptSettingsStore,
};

[[nodiscard]] constexpr std::string_view diagnostic_id_name(DiagnosticId id) noexcept
{
	switch (id) {
	case DiagnosticId::GenericStatus:
		return "GenericStatus";
	case DiagnosticId::FileNotFound:
		return "FileNotFound";
	case DiagnosticId::AccessDenied:
		return "AccessDenied";
	case DiagnosticId::NotAPdfHeader:
		return "NotAPdfHeader";
	case DiagnosticId::DamagedFormat:
		return "DamagedFormat";
	case DiagnosticId::DamagedHeader:
		return "DamagedHeader";
	case DiagnosticId::DamagedToken:
		return "DamagedToken";
	case DiagnosticId::DamagedObject:
		return "DamagedObject";
	case DiagnosticId::DamagedDictionary:
		return "DamagedDictionary";
	case DiagnosticId::DamagedXRef:
		return "DamagedXRef";
	case DiagnosticId::DamagedStream:
		return "DamagedStream";
	case DiagnosticId::DamagedTrailer:
		return "DamagedTrailer";
	case DiagnosticId::RecoveredViaLinearScan:
		return "RecoveredViaLinearScan";
	case DiagnosticId::NeedsPassword:
		return "NeedsPassword";
	case DiagnosticId::WrongPassword:
		return "WrongPassword";
	case DiagnosticId::UnsupportedEncryption:
		return "UnsupportedEncryption";
	case DiagnosticId::UnsupportedFeature:
		return "UnsupportedFeature";
	case DiagnosticId::DecompressionLimitExceeded:
		return "DecompressionLimitExceeded";
	case DiagnosticId::RecursionLimitExceeded:
		return "RecursionLimitExceeded";
	case DiagnosticId::SignatureInvalid:
		return "SignatureInvalid";
	case DiagnosticId::ComplianceFailed:
		return "ComplianceFailed";
	case DiagnosticId::OutOfMemory:
		return "OutOfMemory";
	case DiagnosticId::IoError:
		return "IoError";
	case DiagnosticId::Cancelled:
		return "Cancelled";
	case DiagnosticId::InternalError:
		return "InternalError";
	case DiagnosticId::BlockedLaunchAction:
		return "BlockedLaunchAction";
	case DiagnosticId::BlockedJavaScriptAction:
		return "BlockedJavaScriptAction";
	case DiagnosticId::BlockedSubmitFormAction:
		return "BlockedSubmitFormAction";
	case DiagnosticId::BlockedImportDataAction:
		return "BlockedImportDataAction";
	case DiagnosticId::CorruptRecentsStore:
		return "CorruptRecentsStore";
	case DiagnosticId::CorruptSettingsStore:
		return "CorruptSettingsStore";
	}
	return "GenericStatus";
}

[[nodiscard]] constexpr std::string_view diagnostic_id_message(DiagnosticId id) noexcept
{
	switch (id) {
	case DiagnosticId::GenericStatus:
		return "Engine diagnostic event";
	case DiagnosticId::FileNotFound:
		return "Requested file was not found";
	case DiagnosticId::AccessDenied:
		return "File access was denied";
	case DiagnosticId::NotAPdfHeader:
		return "Input does not start with a valid PDF header";
	case DiagnosticId::DamagedFormat:
		return "Damaged or malformed PDF syntax";
	case DiagnosticId::DamagedHeader:
		return "Damaged PDF header";
	case DiagnosticId::DamagedToken:
		return "Damaged lexical token";
	case DiagnosticId::DamagedObject:
		return "Damaged indirect object";
	case DiagnosticId::DamagedDictionary:
		return "Damaged PDF dictionary";
	case DiagnosticId::DamagedXRef:
		return "Damaged cross-reference table or stream";
	case DiagnosticId::DamagedStream:
		return "Damaged PDF stream object";
	case DiagnosticId::DamagedTrailer:
		return "Damaged PDF trailer";
	case DiagnosticId::RecoveredViaLinearScan:
		return "Reconstructed cross-reference table via fallback linear scan";
	case DiagnosticId::NeedsPassword:
		return "Document is encrypted and requires a password";
	case DiagnosticId::WrongPassword:
		return "Supplied password did not authenticate";
	case DiagnosticId::UnsupportedEncryption:
		return "Document uses an unsupported encryption handler";
	case DiagnosticId::UnsupportedFeature:
		return "Document uses an unsupported PDF feature";
	case DiagnosticId::DecompressionLimitExceeded:
		return "Stream decompression byte limit exceeded";
	case DiagnosticId::RecursionLimitExceeded:
		return "Parser recursion depth limit exceeded";
	case DiagnosticId::SignatureInvalid:
		return "Digital signature verification failed";
	case DiagnosticId::ComplianceFailed:
		return "Document compliance verification failed";
	case DiagnosticId::OutOfMemory:
		return "Memory allocation limit exceeded";
	case DiagnosticId::IoError:
		return "File I/O operation failed";
	case DiagnosticId::Cancelled:
		return "Operation was cancelled";
	case DiagnosticId::InternalError:
		return "Internal engine error";
	case DiagnosticId::BlockedLaunchAction:
		return "Refused external launch action";
	case DiagnosticId::BlockedJavaScriptAction:
		return "Refused PDF JavaScript action";
	case DiagnosticId::BlockedSubmitFormAction:
		return "Refused external form submission action";
	case DiagnosticId::BlockedImportDataAction:
		return "Refused external data import action";
	case DiagnosticId::CorruptRecentsStore:
		return "Recovered from corrupted recent files store";
	case DiagnosticId::CorruptSettingsStore:
		return "Recovered from corrupted settings store";
	}
	return "Engine diagnostic event";
}

[[nodiscard]] constexpr DiagnosticId default_diagnostic_id(ErrorCode code) noexcept
{
	switch (code) {
	case ErrorCode::Ok:
		return DiagnosticId::GenericStatus;
	case ErrorCode::FileNotFound:
		return DiagnosticId::FileNotFound;
	case ErrorCode::AccessDenied:
		return DiagnosticId::AccessDenied;
	case ErrorCode::NotAPdf:
		return DiagnosticId::NotAPdfHeader;
	case ErrorCode::DamagedFormat:
		return DiagnosticId::DamagedFormat;
	case ErrorCode::NeedsPassword:
		return DiagnosticId::NeedsPassword;
	case ErrorCode::WrongPassword:
		return DiagnosticId::WrongPassword;
	case ErrorCode::UnsupportedEncryption:
		return DiagnosticId::UnsupportedEncryption;
	case ErrorCode::UnsupportedFeature:
		return DiagnosticId::UnsupportedFeature;
	case ErrorCode::DecompressionLimitExceeded:
		return DiagnosticId::DecompressionLimitExceeded;
	case ErrorCode::RecursionLimitExceeded:
		return DiagnosticId::RecursionLimitExceeded;
	case ErrorCode::SignatureInvalid:
		return DiagnosticId::SignatureInvalid;
	case ErrorCode::ComplianceFailed:
		return DiagnosticId::ComplianceFailed;
	case ErrorCode::OutOfMemory:
		return DiagnosticId::OutOfMemory;
	case ErrorCode::IoError:
		return DiagnosticId::IoError;
	case ErrorCode::Cancelled:
		return DiagnosticId::Cancelled;
	case ErrorCode::InternalError:
		return DiagnosticId::InternalError;
	}
	return DiagnosticId::GenericStatus;
}

// Closed catalog of ETW span phase identifiers. Only EtwPhaseId enumerators are
// stored in EtwSpan and emitted via TraceLoggingWrite, guaranteeing zero
// document byte payloads and static lifetime safety.
enum class EtwPhaseId : uint16_t {
	UnspecifiedPhase = 0,
	OpenDocument,
	ParseXRef,
	RecoverXRef,
	DecodeFilter,
	DecryptObject,
	InterpretPage,
	BuildDisplayList,
	LoadFont,
	RenderTile,
	ExtractPageText,
	RunOcr,
	ConvertDocument,
	OrganizeDocument,
	SaveDocument,
	SignDocument,
	VerifySignature,
	RedactDocument,
	ValidateCompliance,
};

[[nodiscard]] constexpr std::string_view etw_phase_name(EtwPhaseId phase_id) noexcept
{
	switch (phase_id) {
	case EtwPhaseId::UnspecifiedPhase:
		return "UnspecifiedPhase";
	case EtwPhaseId::OpenDocument:
		return "OpenDocument";
	case EtwPhaseId::ParseXRef:
		return "ParseXRef";
	case EtwPhaseId::RecoverXRef:
		return "RecoverXRef";
	case EtwPhaseId::DecodeFilter:
		return "DecodeFilter";
	case EtwPhaseId::DecryptObject:
		return "DecryptObject";
	case EtwPhaseId::InterpretPage:
		return "InterpretPage";
	case EtwPhaseId::BuildDisplayList:
		return "BuildDisplayList";
	case EtwPhaseId::LoadFont:
		return "LoadFont";
	case EtwPhaseId::RenderTile:
		return "RenderTile";
	case EtwPhaseId::ExtractPageText:
		return "ExtractPageText";
	case EtwPhaseId::RunOcr:
		return "RunOcr";
	case EtwPhaseId::ConvertDocument:
		return "ConvertDocument";
	case EtwPhaseId::OrganizeDocument:
		return "OrganizeDocument";
	case EtwPhaseId::SaveDocument:
		return "SaveDocument";
	case EtwPhaseId::SignDocument:
		return "SignDocument";
	case EtwPhaseId::VerifySignature:
		return "VerifySignature";
	case EtwPhaseId::RedactDocument:
		return "RedactDocument";
	case EtwPhaseId::ValidateCompliance:
		return "ValidateCompliance";
	}
	return "UnspecifiedPhase";
}

[[nodiscard]] EtwPhaseId classify_etw_phase(std::string_view phase_name) noexcept;

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

// Proxy-free inline-first string for PdfError::message so constructing or
// moving PdfError / PdfResult<T> never allocates an MSVC debug container proxy
// inside a noexcept constructor and never terminates under OOM.
class PdfErrorMessage {
public:
	static constexpr size_t kInlineCapacity = 127;

	constexpr PdfErrorMessage() noexcept = default;

	PdfErrorMessage(std::string_view sv) noexcept
	{
		assign(sv.data(), sv.size());
	}

	PdfErrorMessage(const char* s) noexcept
	{
		if (s) {
			assign(s, std::char_traits<char>::length(s));
		}
	}

	PdfErrorMessage(const std::string& s) noexcept
	{
		assign(s.data(), s.size());
	}

	PdfErrorMessage(std::string&& s) noexcept
	{
		assign(s.data(), s.size());
	}

	PdfErrorMessage(const PdfErrorMessage& other) noexcept
	{
		assign(other.data(), other.size());
	}

	PdfErrorMessage(PdfErrorMessage&& other) noexcept
		: size_(other.size_)
		, heap_(other.heap_)
	{
		if (!heap_) {
			for (size_t i = 0; i <= size_; ++i) {
				inline_[i] = other.inline_[i];
			}
		}
		other.heap_ = nullptr;
		other.size_ = 0;
		other.inline_[0] = '\0';
	}

	PdfErrorMessage& operator=(const PdfErrorMessage& other) noexcept
	{
		if (this != &other) {
			assign(other.data(), other.size());
		}
		return *this;
	}

	PdfErrorMessage& operator=(PdfErrorMessage&& other) noexcept
	{
		if (this != &other) {
			reset_heap();
			size_ = other.size_;
			heap_ = other.heap_;
			if (!heap_) {
				for (size_t i = 0; i <= size_; ++i) {
					inline_[i] = other.inline_[i];
				}
			}
			other.heap_ = nullptr;
			other.size_ = 0;
			other.inline_[0] = '\0';
		}
		return *this;
	}

	PdfErrorMessage& operator=(std::string_view sv) noexcept
	{
		assign(sv.data(), sv.size());
		return *this;
	}

	PdfErrorMessage& operator=(const char* s) noexcept
	{
		if (s) {
			assign(s, std::char_traits<char>::length(s));
		} else {
			clear();
		}
		return *this;
	}

	~PdfErrorMessage() noexcept
	{
		reset_heap();
	}

	void clear() noexcept
	{
		reset_heap();
		size_ = 0;
		inline_[0] = '\0';
	}

	void assign(const char* ptr, size_t len) noexcept
	{
		if (!ptr || len == 0) {
			clear();
			return;
		}
		char* old_heap = heap_;
		if (len <= kInlineCapacity) {
			std::memmove(inline_, ptr, len);
			inline_[len] = '\0';
			heap_ = nullptr;
			size_ = len;
			if (old_heap) {
				::operator delete(old_heap);
			}
			return;
		}
		try {
			if (len == static_cast<size_t>(-1)) {
				throw std::bad_alloc();
			}
			char* buf = static_cast<char*>(::operator new(len + 1));
			std::memcpy(buf, ptr, len);
			buf[len] = '\0';
			heap_ = buf;
			size_ = len;
			if (old_heap) {
				::operator delete(old_heap);
			}
		} catch (...) {
			std::memmove(inline_, ptr, kInlineCapacity);
			inline_[kInlineCapacity] = '\0';
			heap_ = nullptr;
			size_ = kInlineCapacity;
			if (old_heap) {
				::operator delete(old_heap);
			}
		}
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

	[[nodiscard]] constexpr const char* data() const noexcept
	{
		return heap_ ? heap_ : inline_;
	}

	[[nodiscard]] constexpr const char* c_str() const noexcept
	{
		return data();
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

	[[nodiscard]] size_t find(std::string_view needle, size_t pos = 0) const noexcept
	{
		return view().find(needle, pos);
	}

	friend constexpr bool operator==(const PdfErrorMessage& a, const PdfErrorMessage& b) noexcept
	{
		return a.view() == b.view();
	}

	friend constexpr bool operator==(const PdfErrorMessage& a, std::string_view b) noexcept
	{
		return a.view() == b;
	}

	friend constexpr bool operator==(std::string_view a, const PdfErrorMessage& b) noexcept
	{
		return a == b.view();
	}

	friend constexpr bool operator==(const PdfErrorMessage& a, const char* b) noexcept
	{
		return a.view() == (b ? std::string_view(b) : std::string_view{});
	}

	friend constexpr bool operator==(const char* a, const PdfErrorMessage& b) noexcept
	{
		return (a ? std::string_view(a) : std::string_view{}) == b.view();
	}

	friend bool operator==(const PdfErrorMessage& a, const std::string& b) noexcept
	{
		return a.view() == std::string_view(b);
	}

	friend bool operator==(const std::string& a, const PdfErrorMessage& b) noexcept
	{
		return std::string_view(a) == b.view();
	}

	friend std::string& operator+=(std::string& lhs, const PdfErrorMessage& rhs)
	{
		lhs.append(rhs.data(), rhs.size());
		return lhs;
	}

private:
	void reset_heap() noexcept
	{
		if (heap_) {
			::operator delete(heap_);
			heap_ = nullptr;
		}
	}

	size_t size_{0};
	char* heap_{nullptr};
	char inline_[kInlineCapacity + 1]{};
};

struct PdfError {
	ErrorCode code{ErrorCode::Ok};
	PdfErrorMessage message{};
	std::optional<uint64_t> byte_offset{std::nullopt};
	std::optional<uint32_t> object_num{std::nullopt};
	uint16_t generation_num{0};

	PdfError() noexcept = default;

	explicit PdfError(
		ErrorCode code_in,
		std::optional<uint64_t> byte_offset_in = std::nullopt,
		std::optional<uint32_t> object_num_in = std::nullopt,
		uint16_t generation_num_in = 0) noexcept
		: code(code_in)
		, byte_offset(byte_offset_in)
		, object_num(object_num_in)
		, generation_num(generation_num_in)
	{
	}

	PdfError(
		ErrorCode code_in,
		std::string&& message_in,
		std::optional<uint64_t> byte_offset_in = std::nullopt,
		std::optional<uint32_t> object_num_in = std::nullopt,
		uint16_t generation_num_in = 0) noexcept
		: code(code_in)
		, message(std::string_view(message_in))
		, byte_offset(byte_offset_in)
		, object_num(object_num_in)
		, generation_num(generation_num_in)
	{
	}

	PdfError(
		ErrorCode code_in,
		std::string_view message_in,
		std::optional<uint64_t> byte_offset_in = std::nullopt,
		std::optional<uint32_t> object_num_in = std::nullopt,
		uint16_t generation_num_in = 0) noexcept
		: code(code_in)
		, message(message_in)
		, byte_offset(byte_offset_in)
		, object_num(object_num_in)
		, generation_num(generation_num_in)
	{
	}

	PdfError(
		ErrorCode code_in,
		const char* message_in,
		std::optional<uint64_t> byte_offset_in = std::nullopt,
		std::optional<uint32_t> object_num_in = std::nullopt,
		uint16_t generation_num_in = 0) noexcept
		: PdfError(
			  code_in,
			  message_in ? std::string_view(message_in) : std::string_view{},
			  byte_offset_in,
			  object_num_in,
			  generation_num_in)
	{
	}

	PdfError(const PdfError&) noexcept = default;
	PdfError& operator=(const PdfError&) noexcept = default;
	PdfError(PdfError&&) noexcept = default;
	PdfError& operator=(PdfError&&) noexcept = default;

	[[nodiscard]] std::string format() const noexcept;

	bool operator==(const PdfError& other) const noexcept = default;
};

namespace detail {

[[noreturn]] void fail_fast_invalid_result_access() noexcept;

} // namespace detail

// Value-or-PdfError container that never throws C++ exceptions across module
// boundaries. If constructing or assigning T throws std::bad_alloc, PdfResult
// captures the failure as ErrorCode::OutOfMemory; any other exception thrown by
// T is captured as ErrorCode::InternalError.
template <typename T>
class [[nodiscard]] PdfResult {
	static_assert(
		!std::is_same_v<std::remove_cvref_t<T>, PdfError>,
		"PdfResult<T> value type cannot be PdfError");

public:
	using value_type = T;
	using error_type = PdfError;

	PdfResult(const T& value) noexcept
		requires std::is_copy_constructible_v<T>
		: storage_(std::in_place_index<0>, PdfError{ErrorCode::InternalError})
	{
		emplace_value_from(value);
	}

	PdfResult(T&& value) noexcept
		requires std::is_move_constructible_v<T>
		: storage_(std::in_place_index<0>, PdfError{ErrorCode::InternalError})
	{
		emplace_value_from(std::move(value));
	}

	PdfResult(const PdfError& error) noexcept
		: storage_(std::in_place_index<0>, error)
	{
	}

	PdfResult(PdfError&& error) noexcept
		: storage_(std::in_place_index<0>, std::move(error))
	{
	}

	PdfResult(const PdfResult& other) noexcept
		requires std::is_copy_constructible_v<T>
		: storage_(std::in_place_index<0>, PdfError{ErrorCode::InternalError})
	{
		if (other.has_value()) {
			emplace_value_from(other.value());
		} else {
			storage_.template emplace<0>(other.error());
		}
	}

	PdfResult(PdfResult&& other) noexcept
		requires std::is_move_constructible_v<T>
		: storage_(std::in_place_index<0>, PdfError{ErrorCode::InternalError})
	{
		if (other.has_value()) {
			emplace_value_from(std::move(other).value());
		} else {
			storage_.template emplace<0>(std::move(other).error());
		}
	}

	PdfResult& operator=(const PdfResult& other) noexcept
		requires std::is_copy_constructible_v<T>
	{
		if (this == &other) {
			return *this;
		}
		if (other.has_value()) {
			assign_value_from(other.value());
		} else {
			storage_.template emplace<0>(other.error());
		}
		return *this;
	}

	PdfResult& operator=(PdfResult&& other) noexcept
		requires std::is_move_constructible_v<T>
	{
		if (this == &other) {
			return *this;
		}
		if (other.has_value()) {
			assign_value_from(std::move(other).value());
		} else {
			storage_.template emplace<0>(std::move(other).error());
		}
		return *this;
	}

	[[nodiscard]] bool has_value() const noexcept
	{
		return storage_.index() == 1;
	}

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return has_value();
	}

	[[nodiscard]] T& value() & noexcept
	{
		auto* ptr = std::get_if<1>(&storage_);
		if (!ptr) {
			detail::fail_fast_invalid_result_access();
		}
		return *ptr;
	}

	[[nodiscard]] const T& value() const& noexcept
	{
		const auto* ptr = std::get_if<1>(&storage_);
		if (!ptr) {
			detail::fail_fast_invalid_result_access();
		}
		return *ptr;
	}

	[[nodiscard]] T&& value() && noexcept
	{
		auto* ptr = std::get_if<1>(&storage_);
		if (!ptr) {
			detail::fail_fast_invalid_result_access();
		}
		return std::move(*ptr);
	}

	[[nodiscard]] const T&& value() const&& noexcept
	{
		const auto* ptr = std::get_if<1>(&storage_);
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
		auto* ptr = std::get_if<0>(&storage_);
		if (!ptr) {
			detail::fail_fast_invalid_result_access();
		}
		return *ptr;
	}

	[[nodiscard]] const PdfError& error() const& noexcept
	{
		const auto* ptr = std::get_if<0>(&storage_);
		if (!ptr) {
			detail::fail_fast_invalid_result_access();
		}
		return *ptr;
	}

	[[nodiscard]] PdfError&& error() && noexcept
	{
		auto* ptr = std::get_if<0>(&storage_);
		if (!ptr) {
			detail::fail_fast_invalid_result_access();
		}
		return std::move(*ptr);
	}

	[[nodiscard]] const PdfError&& error() const&& noexcept
	{
		const auto* ptr = std::get_if<0>(&storage_);
		if (!ptr) {
			detail::fail_fast_invalid_result_access();
		}
		return std::move(*ptr);
	}

	template <typename U>
	[[nodiscard]] T value_or(U&& default_value) const& noexcept
		requires (std::is_nothrow_copy_constructible_v<T> &&
			std::is_nothrow_convertible_v<U&&, T>)
	{
		return has_value() ? value() : static_cast<T>(std::forward<U>(default_value));
	}

	template <typename U>
	[[nodiscard]] T value_or(U&& default_value) && noexcept
		requires (std::is_nothrow_move_constructible_v<T> &&
			std::is_nothrow_convertible_v<U&&, T>)
	{
		return has_value() ? std::move(*this).value() : static_cast<T>(std::forward<U>(default_value));
	}


	template <typename U>
	[[nodiscard]] PdfResult<T> try_value_or(U&& default_value) const& noexcept
		requires std::is_copy_constructible_v<T> && std::is_convertible_v<U&&, T>
	{
		if (has_value()) {
			return PdfResult<T>(value());
		}
		try {
			return PdfResult<T>(static_cast<T>(std::forward<U>(default_value)));
		} catch (const std::bad_alloc&) {
			return PdfError{ErrorCode::OutOfMemory};
		} catch (...) {
			return PdfError{ErrorCode::InternalError};
		}
	}

	template <typename U>
	[[nodiscard]] PdfResult<T> try_value_or(U&& default_value) && noexcept
		requires std::is_move_constructible_v<T> && std::is_convertible_v<U&&, T>
	{
		if (has_value()) {
			return PdfResult<T>(std::move(*this).value());
		}
		try {
			return PdfResult<T>(static_cast<T>(std::forward<U>(default_value)));
		} catch (const std::bad_alloc&) {
			return PdfError{ErrorCode::OutOfMemory};
		} catch (...) {
			return PdfError{ErrorCode::InternalError};
		}
	}


private:
	template <typename Arg>
	void emplace_value_from(Arg&& arg) noexcept
	{
		try {
			storage_.template emplace<1>(std::forward<Arg>(arg));
		} catch (const std::bad_alloc&) {
			storage_.template emplace<0>(PdfError{ErrorCode::OutOfMemory});
		} catch (...) {
			storage_.template emplace<0>(PdfError{ErrorCode::InternalError});
		}
	}

	template <typename Arg>
	void assign_value_from(Arg&& arg) noexcept
	{
		try {
			if constexpr (std::is_assignable_v<T&, Arg&&>) {
				if (auto* existing = std::get_if<1>(&storage_)) {
					*existing = std::forward<Arg>(arg);
					return;
				}
			}
			storage_.template emplace<1>(std::forward<Arg>(arg));
		} catch (const std::bad_alloc&) {
			storage_.template emplace<0>(PdfError{ErrorCode::OutOfMemory});
		} catch (...) {
			storage_.template emplace<0>(PdfError{ErrorCode::InternalError});
		}
	}

	std::variant<PdfError, T> storage_;
};

template <>
class [[nodiscard]] PdfResult<void> {
public:
	using value_type = void;
	using error_type = PdfError;

	PdfResult() noexcept = default;

	PdfResult(const PdfError& error) noexcept
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

struct DiagnosticEntry {
	uint64_t sequence{0};
	uint64_t timestamp_utc_ms{0};
	uint32_t thread_id{0};
	ErrorCode code{ErrorCode::Ok};
	DiagnosticId diagnostic_id{DiagnosticId::GenericStatus};
	std::string message;
	std::optional<uint64_t> byte_offset{std::nullopt};
	std::optional<uint32_t> object_num{std::nullopt};
	uint16_t generation_num{0};

	[[nodiscard]] std::string format() const noexcept;
};

// Maps an ErrorCode and optional diagnostic hint/tag to a closed DiagnosticId
// without retaining caller-supplied bytes.
[[nodiscard]] DiagnosticId classify_diagnostic_id(
	ErrorCode code,
	std::string_view hint = {}) noexcept;

// Records an engine warning or error in the fixed-capacity 256-slot diagnostic
// ring buffer without heap allocation and emits a structured ETW TraceLogging
// event containing only error code, diagnostic ID, offsets, object ID, and
// sequence metadata (zero document byte payloads).
void record_diagnostic(
	ErrorCode code,
	DiagnosticId diagnostic_id,
	std::optional<uint64_t> byte_offset = std::nullopt,
	std::optional<uint32_t> object_num = std::nullopt,
	uint16_t generation_num = 0) noexcept;

void record_diagnostic(
	ErrorCode code,
	std::string_view hint = {},
	std::optional<uint64_t> byte_offset = std::nullopt,
	std::optional<uint32_t> object_num = std::nullopt,
	uint16_t generation_num = 0) noexcept;

void record_diagnostic(const PdfError& err) noexcept;

// Returns up to max_entries of the newest diagnostics in chronological order
// (oldest to newest among the returned entries).
[[nodiscard]] std::vector<DiagnosticEntry> diagnostics_tail(
	size_t max_entries = kDiagnosticRingCapacity) noexcept;

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
	std::string_view message = {},
	std::optional<uint64_t> byte_offset = std::nullopt,
	std::optional<uint32_t> object_num = std::nullopt,
	uint16_t generation_num = 0) noexcept;

// Ensures the "Recto.Diagnostics" ETW TraceLogging provider is registered.
bool ensure_etw_provider_registered() noexcept;

// Whether an ETW session has enabled the provider (TraceLoggingProviderEnabled). Registration alone never enables it:
// events are built and written only when a consumer listens (D16).
[[nodiscard]] bool etw_provider_enabled() noexcept;

// Tests only: enable the provider as a listening session would, so events are written and counted without one.
void force_enable_etw_provider_for_testing() noexcept;

[[nodiscard]] bool is_etw_provider_registered() noexcept;
[[nodiscard]] std::string_view etw_provider_name() noexcept;
[[nodiscard]] uint64_t etw_diagnostic_events_emitted() noexcept;

// Events EventWriteTransfer refused (a non-zero status), counted beside the events emitted.
[[nodiscard]] uint64_t etw_write_failures() noexcept;

// Tests only: make every ETW write report status instead of calling EventWriteTransfer (0 restores real writes).
void set_etw_write_status_for_testing(unsigned long status) noexcept;
[[nodiscard]] uint64_t etw_span_events_emitted() noexcept;

struct CapturedEtwEvent {
	std::string provider_name;
	std::string event_name;
	std::vector<std::string> field_names;
	uint8_t level{0}; // the ETW level the event was written at (WINEVENT_LEVEL_*)
	uint64_t sequence{0};
	uint32_t error_code{0};
	std::string error_code_name;
	uint16_t diagnostic_id{0};
	std::string diagnostic_id_name;
	bool has_byte_offset{false};
	uint64_t byte_offset{0};
	bool has_object_num{false};
	uint32_t object_num{0};
	uint16_t generation_num{0};
	std::string phase;
	uint64_t duration_us{0};
	uint64_t document_id{0};
	uint32_t page_index{0};
	uint32_t thread_id{0};
};

// Enables or disables in-process capture of raw TraceLogging EVENT_DATA_DESCRIPTOR
// packets passed through TLG_EVENT_WRITE_TRANSFER for deterministic verification.
void set_etw_capture_for_testing(bool enabled) noexcept;
void clear_captured_etw_events_for_testing() noexcept;
[[nodiscard]] std::vector<CapturedEtwEvent> captured_etw_events_for_testing() noexcept;

// RAII scoped ETW TraceLogging span for open, render, and job phases (D16).
// Stores only a closed EtwPhaseId enumerator so arbitrary caller strings never
// dangle or leak into ETW events.
class EtwSpan {
public:
	explicit EtwSpan(
		EtwPhaseId phase_id,
		uint64_t document_id = 0,
		uint32_t page_index = 0) noexcept;

	explicit EtwSpan(
		std::string_view phase_name,
		uint64_t document_id = 0,
		uint32_t page_index = 0) noexcept;

	~EtwSpan() noexcept;

	EtwSpan(const EtwSpan&) = delete;
	EtwSpan& operator=(const EtwSpan&) = delete;
	EtwSpan(EtwSpan&&) = delete;
	EtwSpan& operator=(EtwSpan&&) = delete;

	[[nodiscard]] uint64_t elapsed_us() const noexcept;
	[[nodiscard]] EtwPhaseId phase_id() const noexcept { return phase_id_; }
	[[nodiscard]] std::string_view phase_name() const noexcept
	{
		return etw_phase_name(phase_id_);
	}

private:
	EtwPhaseId phase_id_{EtwPhaseId::UnspecifiedPhase};
	uint64_t document_id_{0};
	uint32_t page_index_{0};
	uint32_t thread_id_{0};
	int64_t start_qpc_{0};
};

namespace detail {

template <typename T>
[[nodiscard]] PdfResult<T> materialize_try_result(const PdfResult<T>& res) noexcept
{
	return PdfResult<T>(res);
}

template <typename T>
[[nodiscard]] PdfResult<T> materialize_try_result(PdfResult<T>&& res) noexcept
{
	return PdfResult<T>(std::move(res));
}

} // namespace detail

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

// RECTO_PDF_TRY(decl, expr) moves the value into decl, so it needs a payload whose move cannot throw (a move that
// throws inside a noexcept function terminates). For a payload whose move may throw (MSVC's node-based containers
// allocate when moved), RECTO_PDF_TRY_REF(name, expr) binds name to the value where it already is, with no move.
// Materializing the result is PdfResult's own move or copy, which returns a failure as OutOfMemory or InternalError.
#define RECTO_PDF_TRY_IMPL(tmp_var, var_decl, expr)                                      \
	auto tmp_var = ::recto::pdf::detail::materialize_try_result((expr));                 \
	static_assert(std::is_nothrow_move_constructible_v<                                  \
		typename decltype(tmp_var)::value_type>,                                         \
		"RECTO_PDF_TRY needs a payload whose move cannot throw; use RECTO_PDF_TRY_REF");  \
	if (!tmp_var.has_value()) {                                                          \
		return std::move(tmp_var).error();                                               \
	}                                                                                    \
	var_decl = std::move(tmp_var).value()

#define RECTO_PDF_TRY_REF_IMPL(tmp_var, name, expr)                                      \
	auto tmp_var = ::recto::pdf::detail::materialize_try_result((expr));                 \
	if (!tmp_var.has_value()) {                                                          \
		return std::move(tmp_var).error();                                               \
	}                                                                                    \
	auto& name = tmp_var.value()

#define RECTO_PDF_TRY(var_decl, expr)                                                    \
	RECTO_PDF_TRY_IMPL(RECTO_PDF_CONCAT(_recto_pdf_try_, __LINE__), var_decl, (expr))

#define RECTO_PDF_TRY_REF(name, expr)                                                    \
	RECTO_PDF_TRY_REF_IMPL(RECTO_PDF_CONCAT(_recto_pdf_try_, __LINE__), name, (expr))

#define RECTO_ETW_SPAN(phase_name)                                                       \
	::recto::pdf::EtwSpan RECTO_PDF_CONCAT(_recto_etw_span_, __LINE__)((phase_name))

#define RECTO_ETW_SPAN_DOC(phase_name, doc_id, page_index)                               \
	::recto::pdf::EtwSpan RECTO_PDF_CONCAT(_recto_etw_span_, __LINE__)(                  \
		(phase_name), (doc_id), (page_index))
