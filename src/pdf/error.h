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
	// Appended after InternalError so every earlier numeric value is unchanged (D01 T02 section 18).
	ResourceLimit,   // a bound other than decompression or recursion was reached (commands, change sets, time)
	InvalidArgument, // the caller passed a value outside the documented domain, not a damaged file
	// Appended after InvalidArgument (D01 T02 section 22).
	ConversionFailed,   // a conversion's output failed its own fidelity or validity check and was not written
	RollbackIncomplete, // an operation that replaces files failed and could not restore every original in place
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
	case ErrorCode::ResourceLimit:
		return "ResourceLimit";
	case ErrorCode::InvalidArgument:
		return "InvalidArgument";
	case ErrorCode::ConversionFailed:
		return "ConversionFailed";
	case ErrorCode::RollbackIncomplete:
		return "RollbackIncomplete";
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
	ResourceLimit,
	InvalidArgument,
	ConversionFailed,
	RollbackIncomplete,
	RecoveredDamagedRows,
	RecoveredJpxCodeBlocks,
	RecoveredJpxEmptyComponents,
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
	case DiagnosticId::ResourceLimit:
		return "ResourceLimit";
	case DiagnosticId::InvalidArgument:
		return "InvalidArgument";
	case DiagnosticId::ConversionFailed:
		return "ConversionFailed";
	case DiagnosticId::RollbackIncomplete:
		return "RollbackIncomplete";
	case DiagnosticId::RecoveredDamagedRows:
		return "RecoveredDamagedRows";
	case DiagnosticId::RecoveredJpxCodeBlocks:
		return "RecoveredJpxCodeBlocks";
	case DiagnosticId::RecoveredJpxEmptyComponents:
		return "RecoveredJpxEmptyComponents";
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
	case DiagnosticId::ResourceLimit:
		return "Stopped at a safety limit on work or size; the file may be fine, so try fewer pages or a smaller selection";
	case DiagnosticId::InvalidArgument:
		return "The request was refused before any change because one of its values is not allowed";
	case DiagnosticId::ConversionFailed:
		return "The converted file did not pass its own check, so it was not saved; another conversion mode may work";
	case DiagnosticId::RollbackIncomplete:
		return "The operation failed and some original files could not be put back; their contents are kept in backup "
			   "files beside them";
	case DiagnosticId::RecoveredDamagedRows:
		return "A fax image had damaged rows, so the page still opens with each replaced by the row above when that row "
			   "decoded cleanly and by white when it was damaged too or is the top; count is the rows replaced and first "
			   "the zero-based first one";
	case DiagnosticId::RecoveredJpxCodeBlocks:
		return "A JPEG 2000 image had damaged code-blocks, each kept only down to the bitplane above its damage, so those "
			   "areas lose detail, up to all of it when the top bitplane is damaged; count is the code-blocks, first the "
			   "zero-based ordinal of the first in its tile and component";
	case DiagnosticId::RecoveredJpxEmptyComponents:
		return "A JPEG 2000 tile had no sample of a subsampled component, which the standard allows, so it was filled with "
			   "the mid value; count is such tile-components, with the tile and component of the first";
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
	case ErrorCode::ResourceLimit:
		return DiagnosticId::ResourceLimit;
	case ErrorCode::InvalidArgument:
		return DiagnosticId::InvalidArgument;
	case ErrorCode::ConversionFailed:
		return DiagnosticId::ConversionFailed;
	case ErrorCode::RollbackIncomplete:
		return DiagnosticId::RollbackIncomplete;
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
	case ErrorCode::ResourceLimit:
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

// Which bound, argument or conversion check a ResourceLimit, InvalidArgument or ConversionFailed refusal is about (D01
// T02 sections 23 and 24), or why an image decoder refused damaged data it could not recover (DamagedFormat, D01 T02
// section 26). A closed catalogue of numbers and names, so support can see what stopped the work without the ring or ETW ever holding a path, a
// password or document text. Values are only ever appended.
enum class RefusalReason : uint16_t {
	None = 0,
	CommandCount,   // content-stream commands over the per-page budget
	ChangeSetSize,  // objects in one change set over its budget
	PendingExports, // exports queued at once over the limit
	WallClock,      // work over its time budget, in milliseconds
	WorkingBytes,   // working memory over its budget, in bytes
	PageIndex,        // a page index past the document's last page
	PageRangePastEnd, // a page range reaching past the last page
	OutputSameAsInput, // the output path is the file being read; the path itself is never kept
	// Appended (D01 T02 section 23, completion review): one reason per cause, so each has its own remedy.
	PageRangeEmpty,    // a page range selecting no page
	PageRangeReversed, // a page range whose first page is after its last
	OutputUnwritable,  // the output folder cannot be written; the path itself is never kept
	// Appended (D01 T02 section 24): the conversion read-back checks, so a ConversionFailed error names its check.
	TextRecall,          // re-read text short of the source words, per mille of words matched
	ReadingOrder,        // every word present but out of order, per mille of matched words in order
	EmptyResult,         // a page or the whole output came out empty
	PackageInvalid,      // the written package or file breaks its format's structure
	RelationshipMissing, // an undeclared relationship id or a missing relationship target
	// Appended (D01 T02 section 26): why a CCITT fax decode refused a damaged row instead of substituting it.
	DamagedRowAllowance,   // more consecutive damaged rows than /DamagedRowsBeforeError allows
	DamagedRowNoSync,      // no end-of-line code after the damaged row to resume at
	DamagedRowRecoveryOff, // recovery needs /EndOfLine true and K not negative
	// Appended (D01 T02 section 2): the ceilings on embedded font programs and their glyph outlines.
	FontProgramBytes, // an embedded font program over its byte limit
	GlyphPoints,      // a glyph outline over its point limit
	ComponentVisits,  // composite glyph components visited past the limit
	CompositeDepth,   // composite glyphs nested past the depth limit
	// Appended (D01 T02 section 3): the content-stream interpreter.
	InlineImageFraming, // a filtered inline image whose end was not found within the decoding work limit
	// Appended (D01 T02 section 35): the canonical serializer.
	ObjectValues, // an object with more values, elements or payload bytes than the parser reads back
	// Appended (D01 T02 section 5): the limits a reader puts on an incrementally updated file.
	XrefRevisions, // a file already at the cross-reference revisions a reader follows
	XrefRows,      // cross-reference rows past what a reader reads across all revisions
	ObjectNumber,  // an object number past the highest a reader must accept
	// Appended (D01 T02 section 13): the staged revision chain behind undo.
	HistoryStates, // more edits kept for undo than the history holds
	// Appended (D01 T02 section 29): the ceilings on Type 1 and CFF glyphs.
	CharstringBytes, // a charstring longer than its byte limit
	PathOperators,   // a glyph drawing more path operators than the limit
	CharstringWork,  // a glyph executing more charstring bytes than the limit
	SubroutineDepth, // charstring subroutines nested past the depth limit
	CharstringStack, // a charstring's operand stack past its limit
	FontTableEntries, // a CFF program's subroutine tables past their entry limit
};

[[nodiscard]] constexpr std::string_view refusal_reason_name(RefusalReason reason) noexcept
{
	switch (reason) {
	case RefusalReason::None:
		return "None";
	case RefusalReason::CommandCount:
		return "CommandCount";
	case RefusalReason::ChangeSetSize:
		return "ChangeSetSize";
	case RefusalReason::PendingExports:
		return "PendingExports";
	case RefusalReason::WallClock:
		return "WallClock";
	case RefusalReason::WorkingBytes:
		return "WorkingBytes";
	case RefusalReason::PageIndex:
		return "PageIndex";
	case RefusalReason::PageRangePastEnd:
		return "PageRangePastEnd";
	case RefusalReason::OutputSameAsInput:
		return "OutputSameAsInput";
	case RefusalReason::PageRangeEmpty:
		return "PageRangeEmpty";
	case RefusalReason::PageRangeReversed:
		return "PageRangeReversed";
	case RefusalReason::OutputUnwritable:
		return "OutputUnwritable";
	case RefusalReason::TextRecall:
		return "TextRecall";
	case RefusalReason::ReadingOrder:
		return "ReadingOrder";
	case RefusalReason::EmptyResult:
		return "EmptyResult";
	case RefusalReason::PackageInvalid:
		return "PackageInvalid";
	case RefusalReason::RelationshipMissing:
		return "RelationshipMissing";
	case RefusalReason::DamagedRowAllowance:
		return "DamagedRowAllowance";
	case RefusalReason::DamagedRowNoSync:
		return "DamagedRowNoSync";
	case RefusalReason::DamagedRowRecoveryOff:
		return "DamagedRowRecoveryOff";
	case RefusalReason::FontProgramBytes:
		return "FontProgramBytes";
	case RefusalReason::GlyphPoints:
		return "GlyphPoints";
	case RefusalReason::ComponentVisits:
		return "ComponentVisits";
	case RefusalReason::CompositeDepth:
		return "CompositeDepth";
	case RefusalReason::InlineImageFraming:
		return "InlineImageFraming";
	case RefusalReason::ObjectValues:
		return "ObjectValues";
	case RefusalReason::XrefRevisions:
		return "XrefRevisions";
	case RefusalReason::XrefRows:
		return "XrefRows";
	case RefusalReason::ObjectNumber:
		return "ObjectNumber";
	case RefusalReason::HistoryStates:
		return "HistoryStates";
	case RefusalReason::CharstringBytes:
		return "CharstringBytes";
	case RefusalReason::PathOperators:
		return "PathOperators";
	case RefusalReason::CharstringWork:
		return "CharstringWork";
	case RefusalReason::SubroutineDepth:
		return "SubroutineDepth";
	case RefusalReason::CharstringStack:
		return "CharstringStack";
	case RefusalReason::FontTableEntries:
		return "FontTableEntries";
	}
	return "UnknownReason";
}

// What a refusal reason means to a person reading the log: the cause, the units and conventions of its observed value
// and bound, and what to do. The ring's line for a refusal uses it in place of its diagnostic's general message, so
// every representation says the same thing (D01 T02 section 23). Empty for None.
[[nodiscard]] constexpr std::string_view refusal_reason_description(RefusalReason reason) noexcept
{
	switch (reason) {
	case RefusalReason::None:
		return "";
	case RefusalReason::CommandCount:
		return "The page has more drawing commands than Recto processes at once (observed and bound count commands); "
			   "simplify or split the page";
	case RefusalReason::ChangeSetSize:
		return "One change touches more objects than its limit (observed and bound count objects); make the change in "
			   "smaller steps";
	case RefusalReason::PendingExports:
		return "Too many exports are waiting (observed and bound count exports); wait for some to finish";
	case RefusalReason::WallClock:
		return "The work ran past its time limit (observed and bound are milliseconds); try fewer pages";
	case RefusalReason::WorkingBytes:
		return "The work needed more memory than its limit (observed and bound are bytes); try fewer pages or a smaller "
			   "selection";
	case RefusalReason::PageIndex:
		return "A page past the last one was asked for (observed is the zero-based page index, bound the page count); "
			   "choose a page that exists";
	case RefusalReason::PageRangePastEnd:
		return "The page range reaches past the last page (observed is its last page, one-based, bound the page "
			   "count); end the range at the last page";
	case RefusalReason::OutputSameAsInput:
		return "The output would replace the file being read; choose another name or folder";
	case RefusalReason::PageRangeEmpty:
		return "The page range selects no page; give at least one page";
	case RefusalReason::PageRangeReversed:
		return "The page range starts after it ends (observed is its first page and bound its last, one-based); put "
			   "the lower page first";
	case RefusalReason::OutputUnwritable:
		return "The output folder cannot be written; choose another folder or check its permissions";
	case RefusalReason::TextRecall:
		return "The converted file lost, added or changed words (observed is the per mille of words matched against the "
			   "larger of the source and the result, bound the per mille required); convert the page again or use the exact layout";
	case RefusalReason::ReadingOrder:
		return "The converted file has its words out of order (observed is the per mille of matched words still in "
			   "reading order, bound the per mille required); convert the page again or use the exact layout";
	case RefusalReason::EmptyResult:
		return "The converted file came out empty; check that the page has text, or run OCR on a scanned page first";
	case RefusalReason::PackageInvalid:
		return "The converted file breaks its format's structure and was not kept; convert again, and report it if it "
			   "repeats";
	case RefusalReason::RelationshipMissing:
		return "The converted file refers to a part it does not contain and was not kept; convert again, and report "
			   "it if it repeats";
	case RefusalReason::DamagedRowAllowance:
		return "A fax image has more damaged rows in a row than the file allows to be replaced (observed is the zero-based "
			   "row, bound the /DamagedRowsBeforeError allowance); scan the page again or ask for the original";
	case RefusalReason::DamagedRowNoSync:
		return "A fax image has a damaged row with no end-of-line code after it to resume at (observed is the zero-based "
			   "row, bound the /DamagedRowsBeforeError allowance); scan the page again or ask for the original";
	case RefusalReason::DamagedRowRecoveryOff:
		return "A fax image has a damaged row and its decode settings do not allow damaged rows to be replaced: "
			   "/EndOfLine is false or the image is Group 4 (observed is the zero-based row, bound the "
			   "/DamagedRowsBeforeError allowance); scan the page again or ask for the original";
	case RefusalReason::FontProgramBytes:
		return "An embedded font program is larger than its limit (observed and bound are bytes); the font is not "
			   "used, and its text may not appear as designed";
	case RefusalReason::GlyphPoints:
		return "A glyph outline holds more points than its limit (observed and bound are points); that glyph is not "
			   "drawn, and the file may be damaged";
	case RefusalReason::ComponentVisits:
		return "A composite glyph uses more components than its limit (observed and bound are components visited); "
			   "that glyph is not drawn, and the file may be damaged";
	case RefusalReason::CompositeDepth:
		return "Composite glyphs nest deeper than their limit (observed and bound are levels); that glyph is not "
			   "drawn, and the file may be damaged";
	case RefusalReason::InlineImageFraming:
		return "The end of a compressed image inside the page's content could not be found within the decoding limit "
			   "(observed and bound are bytes decoded); the page is not shown, and the file may be damaged";
	case RefusalReason::ObjectValues:
		return "An object to be written holds more values, entries or text than a PDF object can be read back with "
			   "(observed and bound are values or bytes); nothing is written, and the edit that made it so large "
			   "should be split";
	case RefusalReason::XrefRevisions:
		return "The file already holds as many saved revisions as a PDF reader follows (observed and bound count "
			   "revisions); save a full copy with Save As, which writes one revision";
	case RefusalReason::XrefRows:
		return "The file's cross-reference would hold more entries than a PDF reader reads (observed and bound count "
			   "entries); save a full copy with Save As";
	case RefusalReason::ObjectNumber:
		return "The file has run out of object numbers a PDF reader accepts (observed is the number, bound the "
			   "highest allowed); save a full copy with Save As, which renumbers the objects";
	case RefusalReason::HistoryStates:
		return "The document holds as many unsaved edits as undo keeps (observed and bound count edits); save to keep "
			   "editing";
	case RefusalReason::CharstringBytes:
		return "A glyph program in an embedded Type 1 or CFF font is longer than its limit (observed and bound are "
			   "bytes); that glyph is not drawn, and the file may be damaged";
	case RefusalReason::PathOperators:
		return "A glyph draws more path segments than its limit (observed and bound count segments); that glyph is "
			   "not drawn, and the file may be damaged";
	case RefusalReason::CharstringWork:
		return "A glyph's program runs longer than its limit (observed and bound are bytes executed); that glyph is "
			   "not drawn, and the file may be damaged";
	case RefusalReason::SubroutineDepth:
		return "A glyph's program nests its subroutines deeper than their limit (observed and bound are levels); "
			   "that glyph is not drawn, and the file may be damaged";
	case RefusalReason::CharstringStack:
		return "A glyph's program holds more numbers at once than its limit (observed and bound count numbers); that "
			   "glyph is not drawn, and the file may be damaged";
	case RefusalReason::FontTableEntries:
		return "An embedded font's subroutine tables hold more entries than their limit (observed and bound count "
			   "entries); the font is not used, and its text may not appear as designed";
	}
	return "";
}

// Which decoder step failed (D01 T02 section 25), so support can see where a decode stopped without the message the
// ring never keeps. A closed catalogue of numbers and names; values are only ever appended.
enum class DecoderStep : uint16_t {
	None = 0,
	WicFactory,       // the Windows Imaging Component factory
	WicCreateDecoder, // the WIC stream and decoder
	WicGetFrame,      // the frame and its size and pixel format
	WicConvertFormat, // a format converter
	WicCopyPixels,    // copying the pixels out
	NativeJpx,        // Recto's own JPEG 2000 decoder
	// Font loading (D01 T02 section 2).
	DWriteFactory,       // the shared DirectWrite factory
	DWriteFontLoader,    // the in-memory font file loader and its registration
	DWriteFontReference, // the in-memory font file reference over the font program
	DWriteFontFace,      // the DirectWrite font face
	NativeGlyf,          // Recto's own TrueType glyf outline reader
	// Standard 14 faces (D01 T02 section 27).
	DWriteSystemFont, // the system font collection, its family, font and face
	DWriteFontFile,   // reading a system font file's bytes through its loader
	// Type 1 and CFF programs (D01 T02 section 29).
	NativeCharstring, // Recto's own Type 1 and CFF charstring reader
};

// True for the font-loading steps, whose failures concern a font, not an image.
[[nodiscard]] constexpr bool is_font_step(DecoderStep step) noexcept
{
	return step == DecoderStep::DWriteFactory || step == DecoderStep::DWriteFontLoader ||
		step == DecoderStep::DWriteFontReference || step == DecoderStep::DWriteFontFace ||
		step == DecoderStep::NativeGlyf || step == DecoderStep::DWriteSystemFont || step == DecoderStep::DWriteFontFile ||
		step == DecoderStep::NativeCharstring;
}

// What a font-loading failure means to a person reading the log, by its code (D01 T02 section 2).
[[nodiscard]] constexpr std::string_view font_failure_message(ErrorCode code) noexcept
{
	switch (code) {
	case ErrorCode::OutOfMemory:
		return "Windows ran out of memory while loading a font; close other documents or try fewer pages";
	case ErrorCode::DamagedFormat:
		return "An embedded font program is damaged and was not used";
	case ErrorCode::ResourceLimit:
		return "A glyph in an embedded font needed more work than its limit allows; the file may be fine";
	default:
		return "Windows DirectWrite failed while loading a font";
	}
}

// What a decoder failure means to a person reading the log, by its code, when a decoder step is known: the ring uses it
// in place of the diagnostic's general message, which speaks of documents and configured limits (D01 T02 section 25).
[[nodiscard]] constexpr std::string_view decoder_failure_message(ErrorCode code) noexcept
{
	switch (code) {
	case ErrorCode::OutOfMemory:
		return "Windows ran out of memory while decoding an image; close other documents or try fewer pages";
	case ErrorCode::UnsupportedFeature:
		return "This machine's Windows Imaging Component has no codec or format for an image; the file may be fine";
	case ErrorCode::Cancelled:
		return "An image decode was cancelled; nothing needs repair";
	case ErrorCode::DamagedFormat:
		return "An image decoder rejected the image data";
	case ErrorCode::DecompressionLimitExceeded:
		return "An image needed more memory than its decode limit allows; the file may be fine";
	default:
		return "An image decoder failed";
	}
}

[[nodiscard]] constexpr std::string_view decoder_step_name(DecoderStep step) noexcept
{
	switch (step) {
	case DecoderStep::None:
		return "None";
	case DecoderStep::WicFactory:
		return "WicFactory";
	case DecoderStep::WicCreateDecoder:
		return "WicCreateDecoder";
	case DecoderStep::WicGetFrame:
		return "WicGetFrame";
	case DecoderStep::WicConvertFormat:
		return "WicConvertFormat";
	case DecoderStep::WicCopyPixels:
		return "WicCopyPixels";
	case DecoderStep::NativeJpx:
		return "NativeJpx";
	case DecoderStep::DWriteFactory:
		return "DWriteFactory";
	case DecoderStep::DWriteFontLoader:
		return "DWriteFontLoader";
	case DecoderStep::DWriteFontReference:
		return "DWriteFontReference";
	case DecoderStep::DWriteFontFace:
		return "DWriteFontFace";
	case DecoderStep::NativeGlyf:
		return "NativeGlyf";
	case DecoderStep::DWriteSystemFont:
		return "DWriteSystemFont";
	case DecoderStep::DWriteFontFile:
		return "DWriteFontFile";
	case DecoderStep::NativeCharstring:
		return "NativeCharstring";
	}
	return "UnknownStep";
}

struct PdfError {
	ErrorCode code{ErrorCode::Ok};
	PdfErrorMessage message{};
	std::optional<uint64_t> byte_offset{std::nullopt};
	std::optional<uint32_t> object_num{std::nullopt};
	uint16_t generation_num{0};
	// Appended (D01 T02 section 23): which bound or argument a refusal is about, the value observed and the bound it
	// passed, numbers only.
	RefusalReason refusal_reason{RefusalReason::None};
	std::optional<uint64_t> observed{std::nullopt};
	std::optional<uint64_t> bound{std::nullopt};
	// Appended (D01 T02 section 25): the decoder step that failed and the platform status it returned (a Windows
	// HRESULT), and the same for an earlier attempt this failure followed (WIC before the native JPEG 2000 decoder).
	DecoderStep decoder_step{DecoderStep::None};
	std::optional<uint32_t> platform_status{std::nullopt};
	DecoderStep prior_step{DecoderStep::None};
	std::optional<uint32_t> prior_status{std::nullopt};
	// The zero-based page a refusal is about (D01 T02 section 24); format() shows it one-based.
	std::optional<uint32_t> page_index{std::nullopt};

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
	RefusalReason refusal_reason{RefusalReason::None};
	std::optional<uint64_t> observed{std::nullopt};
	std::optional<uint64_t> bound{std::nullopt};
	DecoderStep decoder_step{DecoderStep::None};
	std::optional<uint32_t> platform_status{std::nullopt};
	DecoderStep prior_step{DecoderStep::None};
	std::optional<uint32_t> prior_status{std::nullopt};
	std::optional<uint32_t> page_index{std::nullopt};
	// A recovery outcome (D01 T02 section 26): how many units were recovered (0 when the entry is not one), the zero-based
	// first, and the tile and component it sits in.
	uint64_t recovered_count{0};
	std::optional<uint64_t> first_recovered{std::nullopt};
	std::optional<uint32_t> tile_index{std::nullopt};
	std::optional<uint32_t> component_index{std::nullopt};

	[[nodiscard]] std::string format() const noexcept;
};

// The decoder context a diagnostic carries (D01 T02 section 25): what PdfError's decoder fields hold.
struct DecoderContext {
	DecoderStep step{DecoderStep::None};
	std::optional<uint32_t> status{std::nullopt};
	DecoderStep prior_step{DecoderStep::None};
	std::optional<uint32_t> prior_status{std::nullopt};
};

// A recovery outcome a diagnostic carries (D01 T02 section 26): the units a decoder recovered instead of refusing (fax
// rows replaced, JPEG 2000 code-blocks whose damaged bitplane was dropped, tile-components filled with the mid value),
// the zero-based first such unit, and its tile and component. Numbers only, never document bytes. count 0 is no outcome.
struct RecoveryContext {
	uint64_t count{0};
	std::optional<uint64_t> first_unit{std::nullopt};
	std::optional<uint32_t> tile{std::nullopt};
	std::optional<uint32_t> component{std::nullopt};
};

// Maps an ErrorCode and optional diagnostic hint/tag to a closed DiagnosticId
// without retaining caller-supplied bytes.
[[nodiscard]] DiagnosticId classify_diagnostic_id(
	ErrorCode code,
	std::string_view hint = {}) noexcept;

// Records an engine warning or error in the fixed-capacity 256-slot diagnostic
// ring buffer without heap allocation and emits a structured ETW TraceLogging
// event containing only error code, diagnostic ID, offsets, object ID, and
// sequence metadata (zero document byte payloads), a refusal's reason, observed value, bound and zero-based page, and a
// recovery's count, first unit, tile and component.
void record_diagnostic(
	ErrorCode code,
	DiagnosticId diagnostic_id,
	std::optional<uint64_t> byte_offset = std::nullopt,
	std::optional<uint32_t> object_num = std::nullopt,
	uint16_t generation_num = 0,
	RefusalReason refusal_reason = RefusalReason::None,
	std::optional<uint64_t> observed = std::nullopt,
	std::optional<uint64_t> bound = std::nullopt,
	const DecoderContext& decoder = {},
	std::optional<uint32_t> page_index = std::nullopt,
	const RecoveryContext& recovery = {}) noexcept;

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
// emits a TraceLoggingWrite event when should_auto_record_diagnostic(code):
// DamagedFormat, DecompressionLimitExceeded, RecursionLimitExceeded and
// ResourceLimit. Other codes, InvalidArgument among them (a caller's error,
// not a damaged file), are recorded only through record_diagnostic. So are
// ConversionFailed and RollbackIncomplete, recorded by the caller once it has
// reported them: the ring and ETW keep the code and its diagnostic, never the
// message, so the failed rule or the original and backup paths reach the user
// through the error's message and the operation's result, not the log.
[[nodiscard]] PdfError make_pdf_error(
	ErrorCode code,
	std::string_view message = {},
	std::optional<uint64_t> byte_offset = std::nullopt,
	std::optional<uint32_t> object_num = std::nullopt,
	uint16_t generation_num = 0) noexcept;

// A ResourceLimit, InvalidArgument or ConversionFailed refusal with its reason, numbers and zero-based page, recorded as
// make_pdf_error records its code (ResourceLimit records itself, with the context; InvalidArgument and ConversionFailed
// are recorded by the caller through record_diagnostic(const PdfError&), which carries the context too). The message
// stays with the caller: the ring and ETW keep only the reason, the observed value, the bound and the page.
[[nodiscard]] PdfError make_refusal_error(
	ErrorCode code,
	RefusalReason reason,
	std::optional<uint64_t> observed,
	std::optional<uint64_t> bound,
	std::string_view message = {},
	std::optional<uint32_t> page_index = std::nullopt) noexcept;

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
	uint16_t refusal_reason{0};
	std::string refusal_reason_name;
	bool has_observed{false};
	uint64_t observed{0};
	bool has_bound{false};
	uint64_t bound{0};
	uint16_t decoder_step{0};
	std::string decoder_step_name;
	bool has_platform_status{false};
	uint32_t platform_status{0};
	uint16_t prior_step{0};
	std::string prior_step_name;
	bool has_prior_status{false};
	uint32_t prior_status{0};
	// A Diagnostic event's zero-based page (D01 T02 section 24), separate from a span's page_index below.
	bool has_diagnostic_page{false};
	uint32_t diagnostic_page_index{0};
	// A Diagnostic event's recovery outcome (D01 T02 section 26).
	uint64_t recovered_count{0};
	bool has_first_recovered{false};
	uint64_t first_recovered{0};
	bool has_tile{false};
	uint32_t tile_index{0};
	bool has_component{false};
	uint32_t component_index{0};
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
