// Tests for PdfResult<T>, PdfError, ErrorCode, D16 diagnostic ring buffer,
// and ETW TraceLogging provider/spans (D01 T01 section 1).
#include "error.h"
#include "test_support.h"

#include <windows.h>
#include <evntrace.h>
#include <winmeta.h>


#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace recto::pdf {
namespace {

PdfResult<int> add_checked(int a, int b)
{
	if (a < 0 || b < 0) {
		return PdfError{ErrorCode::DamagedFormat, "negative operand", 42, 7, 0};
	}
	return a + b;
}

PdfResult<int> chain_add_checked(int a, int b, int c)
{
	RECTO_PDF_TRY(const int ab, add_checked(a, b));
	RECTO_PDF_TRY(const int abc, add_checked(ab, c));
	return abc;
}

PdfResult<void> validate_non_negative(int v)
{
	if (v < 0) {
		return PdfError{ErrorCode::DamagedFormat, "negative value"};
	}
	return PdfResult<void>::ok();
}

PdfResult<void> chain_validate(int a, int b)
{
	RECTO_PDF_RETURN_IF_ERROR(validate_non_negative(a));
	RECTO_PDF_RETURN_IF_ERROR(validate_non_negative(b));
	return PdfResult<void>::ok();
}

struct ThrowingPayload {
	static inline bool throw_non_std = false;
	static inline bool throw_bad_alloc = false;

	int v{0};

	ThrowingPayload() noexcept = default;
	explicit ThrowingPayload(int x) noexcept : v(x) {}

	ThrowingPayload(const ThrowingPayload& other)
		: v(other.v)
	{
		if (throw_bad_alloc) {
			throw std::bad_alloc();
		}
		if (throw_non_std) {
			throw 1;
		}
	}

	ThrowingPayload(ThrowingPayload&& other)
		: v(other.v)
	{
		if (throw_bad_alloc) {
			throw std::bad_alloc();
		}
		if (throw_non_std) {
			throw 1;
		}
	}

	ThrowingPayload& operator=(const ThrowingPayload& other)
	{
		if (throw_bad_alloc) {
			throw std::bad_alloc();
		}
		if (throw_non_std) {
			throw 1;
		}
		v = other.v;
		return *this;
	}

	// Non-throwing move assignment paired with potentially throwing move
	// construction, exercising variant state transitions from error -> value.
	ThrowingPayload& operator=(ThrowingPayload&& other) noexcept
	{
		v = other.v;
		return *this;
	}
};

struct NonDefaultThrowingPayload {
	static inline bool fail_alloc = false;
	static inline bool fail_other = false;

	int v{0};

	NonDefaultThrowingPayload() = delete;
	explicit NonDefaultThrowingPayload(int x) noexcept : v(x) {}

	NonDefaultThrowingPayload(const NonDefaultThrowingPayload& other)
		: v(other.v)
	{
		if (fail_alloc) {
			throw std::bad_alloc{};
		}
		if (fail_other) {
			throw 7;
		}
	}

	NonDefaultThrowingPayload(NonDefaultThrowingPayload&& other)
		: v(other.v)
	{
		if (fail_alloc) {
			throw std::bad_alloc{};
		}
		if (fail_other) {
			throw 7;
		}
	}
};

PdfResult<int> extract_non_default_lvalue(PdfResult<NonDefaultThrowingPayload>& r) noexcept
{
	RECTO_PDF_TRY_REF(value, r);
	return value.v;
}

PdfResult<int> extract_non_default_rvalue(PdfResult<NonDefaultThrowingPayload>&& r) noexcept
{
	RECTO_PDF_TRY_REF(value, std::move(r));
	return value.v;
}

// value_or returns a value, as std::expected does, so a temporary fallback cannot leave a dangling reference. On a
// const result it copies, so it exists only for payloads whose copy cannot throw; others use try_value_or, which
// returns a failed copy as PdfError, or the rvalue form, which moves.
static_assert(std::is_same_v<decltype(std::declval<const PdfResult<int>&>().value_or(7)), int>);
static_assert(std::is_same_v<decltype(std::declval<PdfResult<std::string>&&>().value_or(std::string{})), std::string>);

TEST(PdfError, ValueOrReturnsAValue)
{
	PdfResult<std::string> failed{PdfError{ErrorCode::IoError}};
	const std::string fallback = std::move(failed).value_or(std::string(1024, 'x'));
	EXPECT_EQ(fallback, std::string(1024, 'x'));
	const PdfResult<int> failed_int{PdfError{ErrorCode::IoError}};
	EXPECT_EQ(failed_int.value_or(7), 7);
	const PdfResult<std::string> ok{std::string("kept")};
	const auto copied = ok.try_value_or(std::string("unused"));
	ASSERT_TRUE(copied.has_value());
	EXPECT_EQ(*copied, "kept");
}

// Whether an ETW session enables the Recto.Diagnostics provider in this process, as the OS records it per provider
// instance (EnumerateTraceGuidsEx, TraceGuidQueryInfo: no administrator rights needed). Enablement faked inside the
// process never shows here.
bool os_session_enables_recto_provider()
{
	GUID guid = {0x8c4e7f92, 0x3b1a, 0x4d5e, {0x9a, 0x21, 0x7c, 0x6b, 0x5d, 0x4e, 0x3f, 0x10}}; // error.cpp
	ULONG size = 0;
	ULONG rc = EnumerateTraceGuidsEx(TraceGuidQueryInfo, &guid, sizeof(guid), nullptr, 0, &size);
	if (rc != ERROR_INSUFFICIENT_BUFFER || size == 0) {
		return false; // no instance the OS knows of, so no session enables one
	}
	std::vector<BYTE> buffer(size);
	rc = EnumerateTraceGuidsEx(TraceGuidQueryInfo, &guid, sizeof(guid), buffer.data(), size, &size);
	if (rc != ERROR_SUCCESS) {
		return false;
	}
	const auto* info = reinterpret_cast<const TRACE_GUID_INFO*>(buffer.data());
	const auto* instance = reinterpret_cast<const TRACE_PROVIDER_INSTANCE_INFO*>(info + 1);
	for (ULONG i = 0; i < info->InstanceCount; ++i) {
		if (instance->Pid == GetCurrentProcessId() && instance->EnableCount > 0) {
			return true;
		}
		if (instance->NextOffset == 0) {
			break;
		}
		instance = reinterpret_cast<const TRACE_PROVIDER_INSTANCE_INFO*>(
			reinterpret_cast<const BYTE*>(instance) + instance->NextOffset);
	}
	return false;
}

// Registration never enables the provider by itself: it is enabled exactly when a session listens (D16). Checked in a
// fresh process (a death-test child), since other tests here force it on through the test seam, and compared with the
// OS's own record, so a session that happens to listen on this machine does not change the answer.
TEST(PdfError, EtwProviderEnabledOnlyByASession)
{
	EXPECT_EXIT(
		{
			if (!ensure_etw_provider_registered()) {
				std::exit(2);
			}
			std::exit(etw_provider_enabled() == os_session_enables_recto_provider() ? 0 : 3);
		},
		::testing::ExitedWithCode(0), "");
}

// A write Windows refuses is counted as a failure, so the tests' "accepted every write" checks can fail.
TEST(PdfError, EtwRefusedWritesAreCounted)
{
	force_enable_etw_provider_for_testing();
	const uint64_t before = etw_write_failures();
	set_etw_write_status_for_testing(ERROR_INVALID_PARAMETER);
	record_diagnostic(ErrorCode::DamagedFormat, "refused write", 0, 1, 0);
	set_etw_write_status_for_testing(0);
	EXPECT_EQ(etw_write_failures(), before + 1);
	record_diagnostic(ErrorCode::DamagedFormat, "accepted write", 0, 1, 0);
	EXPECT_EQ(etw_write_failures(), before + 1);
}

TEST(PdfError, ResultValueAndError)
{
	// Counted events are the ones written, which needs an enabled provider; no session listens in tests.
	force_enable_etw_provider_for_testing();
	clear_diagnostics();

	constexpr std::array<ErrorCode, 17> kAllCodes = {
		ErrorCode::Ok,
		ErrorCode::FileNotFound,
		ErrorCode::AccessDenied,
		ErrorCode::NotAPdf,
		ErrorCode::DamagedFormat,
		ErrorCode::NeedsPassword,
		ErrorCode::WrongPassword,
		ErrorCode::UnsupportedEncryption,
		ErrorCode::UnsupportedFeature,
		ErrorCode::DecompressionLimitExceeded,
		ErrorCode::RecursionLimitExceeded,
		ErrorCode::SignatureInvalid,
		ErrorCode::ComplianceFailed,
		ErrorCode::OutOfMemory,
		ErrorCode::IoError,
		ErrorCode::Cancelled,
		ErrorCode::InternalError,
	};

	std::set<std::string_view> seen_names;
	for (const ErrorCode code : kAllCodes) {
		const std::string_view name = error_code_name(code);
		EXPECT_FALSE(name.empty());
		EXPECT_NE(name, "UnknownError");
		EXPECT_TRUE(seen_names.insert(name).second) << "Duplicate name: " << name;
	}
	EXPECT_EQ(
		error_code_name(static_cast<ErrorCode>(9999u)),
		std::string_view("UnknownError"));

	// PdfError::format() formatting permutations.
	const PdfError plain_err{ErrorCode::NotAPdf, "Missing %PDF- header"};
	EXPECT_EQ(plain_err.format(), "[NotAPdf] Missing %PDF- header");

	const PdfError offset_err{ErrorCode::DamagedFormat, "Bad xref keyword", 1024, std::nullopt, 0};
	EXPECT_EQ(offset_err.format(), "[DamagedFormat] Bad xref keyword (offset=1024)");

	const PdfError obj_err{ErrorCode::DamagedFormat, "Invalid stream dict", std::nullopt, 15, 2};
	EXPECT_EQ(obj_err.format(), "[DamagedFormat] Invalid stream dict (obj=15 2 R)");

	const PdfError full_err{ErrorCode::DamagedFormat, "Corrupt token", 4096, 12, 0};
	EXPECT_EQ(full_err.format(), "[DamagedFormat] Corrupt token (offset=4096, obj=12 0 R)");

	// Copyable PdfResult<T>
	PdfResult<int> ok_int = 42;
	EXPECT_TRUE(ok_int.has_value());
	EXPECT_TRUE(static_cast<bool>(ok_int));
	EXPECT_EQ(ok_int.value(), 42);
	EXPECT_EQ(*ok_int, 42);
	EXPECT_EQ(ok_int.value_or(0), 42);
	RECTO_ASSERT_OK(ok_int);

	PdfResult<int> err_int = PdfError{ErrorCode::NeedsPassword, "Encrypted document"};
	EXPECT_FALSE(err_int.has_value());
	EXPECT_FALSE(static_cast<bool>(err_int));
	EXPECT_EQ(err_int.error().code, ErrorCode::NeedsPassword);
	EXPECT_EQ(err_int.value_or(99), 99);
	RECTO_EXPECT_ERR(err_int, ErrorCode::NeedsPassword);

	// Move-only PdfResult<std::unique_ptr<int>>
	PdfResult<std::unique_ptr<int>> ok_ptr = std::make_unique<int>(777);
	ASSERT_TRUE(ok_ptr.has_value());
	EXPECT_EQ(**ok_ptr, 777);
	std::unique_ptr<int> moved_ptr = std::move(ok_ptr).value();
	ASSERT_NE(moved_ptr, nullptr);
	EXPECT_EQ(*moved_ptr, 777);

	PdfResult<std::unique_ptr<int>> err_ptr =
		PdfError{ErrorCode::OutOfMemory, "Allocation failed"};
	EXPECT_FALSE(err_ptr.has_value());
	std::unique_ptr<int> fallback_ptr =
		std::move(err_ptr).value_or(std::make_unique<int>(123));
	ASSERT_NE(fallback_ptr, nullptr);
	EXPECT_EQ(*fallback_ptr, 123);

	// PdfResult<void> and RECTO_PDF_TRY / RECTO_PDF_RETURN_IF_ERROR
	PdfResult<void> ok_void = PdfResult<void>::ok();
	EXPECT_TRUE(ok_void.has_value());
	ok_void.value();
	RECTO_ASSERT_OK(ok_void);

	PdfResult<void> err_void = PdfError{ErrorCode::Cancelled, "Operation cancelled"};
	EXPECT_FALSE(err_void.has_value());
	RECTO_EXPECT_ERR(err_void, ErrorCode::Cancelled);

	RECTO_ASSERT_OK(chain_add_checked(10, 20, 30));
	EXPECT_EQ(chain_add_checked(10, 20, 30).value(), 60);
	RECTO_EXPECT_ERR(chain_add_checked(10, -5, 30), ErrorCode::DamagedFormat);

	RECTO_ASSERT_OK(chain_validate(1, 2));
	RECTO_EXPECT_ERR(chain_validate(1, -1), ErrorCode::DamagedFormat);

	// No-exceptions boundary guarantee: throwing copy/move constructors and
	// error->value move-assignment never propagate exceptions or call terminate;
	// std::bad_alloc maps to ErrorCode::OutOfMemory and other exceptions map to
	// ErrorCode::InternalError.
	ThrowingPayload::throw_non_std = false;
	ThrowingPayload::throw_bad_alloc = false;
	PdfResult<ThrowingPayload> source{ThrowingPayload{55}};
	ASSERT_TRUE(source.has_value());
	EXPECT_EQ(source->v, 55);

	PdfResult<ThrowingPayload> target{PdfError{ErrorCode::IoError}};
	ThrowingPayload::throw_non_std = true;
	target = std::move(source);
	EXPECT_FALSE(target.has_value());
	EXPECT_EQ(target.error().code, ErrorCode::InternalError);

	const ThrowingPayload plain_payload{88};
	PdfResult<ThrowingPayload> copy_ctor_res{plain_payload};
	EXPECT_FALSE(copy_ctor_res.has_value());
	EXPECT_EQ(copy_ctor_res.error().code, ErrorCode::InternalError);

	ThrowingPayload::throw_non_std = false;
	ThrowingPayload::throw_bad_alloc = true;
	PdfResult<ThrowingPayload> bad_alloc_ctor{plain_payload};
	EXPECT_FALSE(bad_alloc_ctor.has_value());
	EXPECT_EQ(bad_alloc_ctor.error().code, ErrorCode::OutOfMemory);

	ThrowingPayload::throw_bad_alloc = false;
	PdfResult<ThrowingPayload> good_source{ThrowingPayload{99}};
	PdfResult<ThrowingPayload> bad_alloc_target{PdfError{ErrorCode::IoError}};
	ThrowingPayload::throw_bad_alloc = true;
	bad_alloc_target = good_source;
	EXPECT_FALSE(bad_alloc_target.has_value());
	EXPECT_EQ(bad_alloc_target.error().code, ErrorCode::OutOfMemory);

	// value_or returns a value, so it exists only for payloads whose copy cannot throw (none
	// of the reference-returning form that could dangle); try_value_or captures copy, move
	// and conversion failures into PdfResult<T>.
	const ThrowingPayload fallback_val{321};

	const auto tried_copy = good_source.try_value_or(fallback_val);
	EXPECT_FALSE(tried_copy.has_value());
	EXPECT_EQ(tried_copy.error().code, ErrorCode::OutOfMemory);

	ThrowingPayload::throw_bad_alloc = false;
	ThrowingPayload::throw_non_std = true;
	const auto tried_move = std::move(good_source).try_value_or(ThrowingPayload{654});
	EXPECT_FALSE(tried_move.has_value());
	EXPECT_EQ(tried_move.error().code, ErrorCode::InternalError);
	ThrowingPayload::throw_non_std = false;

	// Non-default-constructible payload throwing std::bad_alloc during try_value_or and
	// RECTO_PDF_TRY_REF materialization.
	NonDefaultThrowingPayload::fail_alloc = false;
	NonDefaultThrowingPayload::fail_other = false;
	PdfResult<NonDefaultThrowingPayload> nd_ok{NonDefaultThrowingPayload{11}};
	PdfResult<NonDefaultThrowingPayload> nd_err{PdfError{ErrorCode::IoError}};
	const NonDefaultThrowingPayload nd_fallback{22};

	NonDefaultThrowingPayload::fail_alloc = true;
	EXPECT_EQ(nd_ok.try_value_or(nd_fallback).error().code, ErrorCode::OutOfMemory);
	EXPECT_EQ(extract_non_default_lvalue(nd_ok).error().code, ErrorCode::OutOfMemory);
	EXPECT_EQ(extract_non_default_rvalue(std::move(nd_ok)).error().code, ErrorCode::OutOfMemory);

	NonDefaultThrowingPayload::fail_alloc = false;
	PdfResult<NonDefaultThrowingPayload> nd_ok2{NonDefaultThrowingPayload{33}};
	NonDefaultThrowingPayload::fail_other = true;
	EXPECT_EQ(extract_non_default_lvalue(nd_ok2).error().code, ErrorCode::InternalError);
	NonDefaultThrowingPayload::fail_other = false;

	// Automatic diagnostic recording on make_pdf_error for DamagedFormat,
	// DecompressionLimitExceeded, and RecursionLimitExceeded.
	clear_diagnostics();
	const uint64_t diag_events_before = etw_diagnostic_events_emitted();
	const uint64_t write_failures_before = etw_write_failures();

	const PdfError non_auto = make_pdf_error(ErrorCode::FileNotFound, "missing.pdf");
	EXPECT_EQ(non_auto.code, ErrorCode::FileNotFound);
	EXPECT_TRUE(diagnostics_tail(10).empty());

	const PdfError auto1 =
		make_pdf_error(ErrorCode::DamagedFormat, "bad dictionary", 128, 4, 0);
	const PdfError auto2 =
		make_pdf_error(ErrorCode::DecompressionLimitExceeded, "stream exceeds 256 MB", 512, 9, 0);
	const PdfError auto3 =
		make_pdf_error(ErrorCode::RecursionLimitExceeded, "nesting depth > 64", 768, 11, 1);

	const auto tail = diagnostics_tail(10);
	ASSERT_EQ(tail.size(), 3u);
	EXPECT_EQ(tail[0].code, auto1.code);
	EXPECT_EQ(tail[0].diagnostic_id, DiagnosticId::DamagedDictionary);
	EXPECT_EQ(tail[0].message, diagnostic_id_message(DiagnosticId::DamagedDictionary));
	EXPECT_EQ(tail[0].byte_offset, std::optional<uint64_t>(128));
	EXPECT_EQ(tail[0].object_num, std::optional<uint32_t>(4));
	EXPECT_EQ(tail[0].generation_num, 0u);

	EXPECT_EQ(tail[1].code, auto2.code);
	EXPECT_EQ(tail[1].diagnostic_id, DiagnosticId::DecompressionLimitExceeded);
	EXPECT_EQ(tail[1].message, diagnostic_id_message(DiagnosticId::DecompressionLimitExceeded));
	EXPECT_EQ(tail[1].byte_offset, std::optional<uint64_t>(512));
	EXPECT_EQ(tail[1].object_num, std::optional<uint32_t>(9));

	EXPECT_EQ(tail[2].code, auto3.code);
	EXPECT_EQ(tail[2].diagnostic_id, DiagnosticId::RecursionLimitExceeded);
	EXPECT_EQ(tail[2].message, diagnostic_id_message(DiagnosticId::RecursionLimitExceeded));
	EXPECT_EQ(tail[2].byte_offset, std::optional<uint64_t>(768));
	EXPECT_EQ(tail[2].object_num, std::optional<uint32_t>(11));
	EXPECT_EQ(tail[2].generation_num, 1u);

	EXPECT_GE(etw_diagnostic_events_emitted(), diag_events_before + 3);
	EXPECT_EQ(etw_write_failures(), write_failures_before); // Windows accepted every write
}

// Codes are only ever appended: every numeric value from Ok up names its fixed code, uniquely, and maps to a
// diagnostic of its own, so a renumbered or unnamed enumerator fails here (D01 T02 sections 18 and 22).
TEST(PdfError, ErrorCodeNamesCoverEveryCode)
{
	constexpr std::array<std::string_view, 21> kNamesByValue = {
		"Ok",
		"FileNotFound",
		"AccessDenied",
		"NotAPdf",
		"DamagedFormat",
		"NeedsPassword",
		"WrongPassword",
		"UnsupportedEncryption",
		"UnsupportedFeature",
		"DecompressionLimitExceeded",
		"RecursionLimitExceeded",
		"SignatureInvalid",
		"ComplianceFailed",
		"OutOfMemory",
		"IoError",
		"Cancelled",
		"InternalError",
		"ResourceLimit",
		"InvalidArgument",
		"ConversionFailed",
		"RollbackIncomplete",
	};
	static_assert(noexcept(error_code_name(ErrorCode::Ok)));
	static_assert(error_code_name(ErrorCode::ResourceLimit) == "ResourceLimit");
	static_assert(error_code_name(ErrorCode::InvalidArgument) == "InvalidArgument");
	static_assert(error_code_name(ErrorCode::ConversionFailed) == "ConversionFailed");
	static_assert(error_code_name(ErrorCode::RollbackIncomplete) == "RollbackIncomplete");
	static_assert(static_cast<uint32_t>(ErrorCode::InvalidArgument) == 18u); // appended after it, nothing renumbered

	std::set<std::string_view> seen;
	for (uint32_t value = 0; value < kNamesByValue.size(); ++value) {
		const auto code = static_cast<ErrorCode>(value);
		EXPECT_EQ(error_code_name(code), kNamesByValue[value]) << "value " << value;
		EXPECT_TRUE(seen.insert(error_code_name(code)).second) << "duplicate name at value " << value;
		if (code != ErrorCode::Ok) {
			EXPECT_NE(default_diagnostic_id(code), DiagnosticId::GenericStatus) << kNamesByValue[value];
		}
	}
	EXPECT_EQ(error_code_name(static_cast<ErrorCode>(kNamesByValue.size())), "UnknownError");
	EXPECT_EQ(default_diagnostic_id(ErrorCode::ResourceLimit), DiagnosticId::ResourceLimit);
	EXPECT_EQ(default_diagnostic_id(ErrorCode::InvalidArgument), DiagnosticId::InvalidArgument);
	EXPECT_EQ(diagnostic_id_name(DiagnosticId::ResourceLimit), "ResourceLimit");
	EXPECT_EQ(diagnostic_id_name(DiagnosticId::InvalidArgument), "InvalidArgument");
	EXPECT_NE(diagnostic_id_message(DiagnosticId::ResourceLimit), diagnostic_id_message(DiagnosticId::GenericStatus));
	EXPECT_NE(diagnostic_id_message(DiagnosticId::InvalidArgument), diagnostic_id_message(DiagnosticId::GenericStatus));
	EXPECT_EQ(default_diagnostic_id(ErrorCode::ConversionFailed), DiagnosticId::ConversionFailed);
	EXPECT_EQ(default_diagnostic_id(ErrorCode::RollbackIncomplete), DiagnosticId::RollbackIncomplete);
	EXPECT_EQ(diagnostic_id_name(DiagnosticId::ConversionFailed), "ConversionFailed");
	EXPECT_EQ(diagnostic_id_name(DiagnosticId::RollbackIncomplete), "RollbackIncomplete");
	EXPECT_NE(diagnostic_id_message(DiagnosticId::ConversionFailed).find("not saved"), std::string_view::npos);
	EXPECT_NE(diagnostic_id_message(DiagnosticId::RollbackIncomplete).find("backup"), std::string_view::npos);
}

// ConversionFailed and RollbackIncomplete are recorded by the caller that knows the failed rule or the backup paths,
// never automatically, and a message that reads like a damage hint never turns either into a damaged-file diagnostic
// (D01 T02 section 22).
TEST(PdfError, ConversionAndRollbackCodesRecordOnlyWhenLogged)
{
	clear_diagnostics();
	const PdfError failed = make_pdf_error(ErrorCode::ConversionFailed, "bad stream");
	const PdfError rollback = make_pdf_error(ErrorCode::RollbackIncomplete, "bad xref");
	EXPECT_TRUE(diagnostics_tail(10).empty());
	record_diagnostic(failed);
	record_diagnostic(rollback);
	const auto tail = diagnostics_tail(10);
	ASSERT_EQ(tail.size(), 2u);
	EXPECT_EQ(tail[0].diagnostic_id, DiagnosticId::ConversionFailed);
	EXPECT_EQ(tail[1].diagnostic_id, DiagnosticId::RollbackIncomplete);
	for (const DiagnosticId id : {DiagnosticId::ConversionFailed, DiagnosticId::RollbackIncomplete}) {
		const std::string_view message = diagnostic_id_message(id);
		EXPECT_EQ(message.find("amage"), std::string_view::npos) << message;
	}
	clear_diagnostics();
}

// ResourceLimit is a bound reached on document content and records itself like the other limits; InvalidArgument is
// a caller's error and records only when logged. Both read back from the ring with their names.
TEST(PdfError, DiagnosticRingRecordsNewCodes)
{
	force_enable_etw_provider_for_testing();
	clear_diagnostics();
	const uint64_t events_before = etw_diagnostic_events_emitted();

	const PdfError limit = make_pdf_error(ErrorCode::ResourceLimit, "command count over budget", 64, 3, 0);
	const PdfError bad_argument = make_pdf_error(ErrorCode::InvalidArgument, "page index past the end");
	auto tail = diagnostics_tail(10);
	ASSERT_EQ(tail.size(), 1u);
	EXPECT_EQ(tail[0].code, limit.code);
	EXPECT_EQ(tail[0].diagnostic_id, DiagnosticId::ResourceLimit);
	EXPECT_EQ(tail[0].byte_offset, std::optional<uint64_t>(64));
	EXPECT_EQ(tail[0].object_num, std::optional<uint32_t>(3));

	record_diagnostic(bad_argument);
	tail = diagnostics_tail(10);
	ASSERT_EQ(tail.size(), 2u);
	EXPECT_EQ(tail[1].code, ErrorCode::InvalidArgument);
	EXPECT_EQ(tail[1].diagnostic_id, DiagnosticId::InvalidArgument);
	EXPECT_NE(tail[0].format().find("ResourceLimit/ResourceLimit"), std::string::npos) << tail[0].format();
	EXPECT_NE(tail[1].format().find("InvalidArgument/InvalidArgument"), std::string::npos) << tail[1].format();
	EXPECT_GE(etw_diagnostic_events_emitted(), events_before + 2);

	// A message that reads like a damage hint never turns either code into a damaged-file diagnostic, and neither
	// code's wording blames the file.
	clear_diagnostics();
	const PdfError hinted = make_pdf_error(ErrorCode::ResourceLimit, "bad stream");
	record_diagnostic(ErrorCode::InvalidArgument, "bad xref");
	tail = diagnostics_tail(10);
	ASSERT_EQ(tail.size(), 2u);
	EXPECT_EQ(tail[0].diagnostic_id, DiagnosticId::ResourceLimit);
	EXPECT_EQ(tail[1].diagnostic_id, DiagnosticId::InvalidArgument);
	EXPECT_EQ(hinted.code, ErrorCode::ResourceLimit);
	for (const DiagnosticId id : {DiagnosticId::ResourceLimit, DiagnosticId::InvalidArgument}) {
		const std::string_view message = diagnostic_id_message(id);
		EXPECT_EQ(message.find("amage"), std::string_view::npos) << message;
		EXPECT_EQ(message.find("alformed"), std::string_view::npos) << message;
	}
	EXPECT_NE(diagnostic_id_message(DiagnosticId::InvalidArgument).find("refused before any change"), std::string_view::npos);

	// The ETW packets carry each code's own number, name and diagnostic, and the context given.
	clear_diagnostics();
	clear_captured_etw_events_for_testing();
	set_etw_capture_for_testing(true);
	(void)make_pdf_error(ErrorCode::ResourceLimit, "command count over budget", 64, 3, 0);
	record_diagnostic(ErrorCode::InvalidArgument, "page index past the end", 128, 5, 1);
	set_etw_capture_for_testing(false);
	const auto captured = captured_etw_events_for_testing();
	ASSERT_EQ(captured.size(), 2u);
	EXPECT_EQ(captured[0].event_name, "Diagnostic");
	EXPECT_EQ(captured[0].error_code, static_cast<uint32_t>(ErrorCode::ResourceLimit));
	EXPECT_EQ(captured[0].error_code_name, "ResourceLimit");
	EXPECT_EQ(captured[0].diagnostic_id, static_cast<uint16_t>(DiagnosticId::ResourceLimit));
	EXPECT_EQ(captured[0].diagnostic_id_name, "ResourceLimit");
	EXPECT_EQ(captured[0].byte_offset, 64u);
	EXPECT_EQ(captured[0].object_num, 3u);
	EXPECT_EQ(captured[1].event_name, "Diagnostic");
	EXPECT_EQ(captured[1].error_code, static_cast<uint32_t>(ErrorCode::InvalidArgument));
	EXPECT_EQ(captured[1].error_code_name, "InvalidArgument");
	EXPECT_EQ(captured[1].diagnostic_id, static_cast<uint16_t>(DiagnosticId::InvalidArgument));
	EXPECT_EQ(captured[1].diagnostic_id_name, "InvalidArgument");
	EXPECT_EQ(captured[1].byte_offset, 128u);
	EXPECT_EQ(captured[1].object_num, 5u);
	EXPECT_EQ(captured[1].generation_num, 1u);
	clear_captured_etw_events_for_testing();
	clear_diagnostics();
}

TEST(PdfError, DiagnosticRingWraparound)
{
	// Counted events are the ones written, which needs an enabled provider; no session listens in tests.
	force_enable_etw_provider_for_testing();
	clear_diagnostics();
	EXPECT_EQ(diagnostics_total_recorded(), 0u);
	EXPECT_TRUE(diagnostics_tail().empty());
	EXPECT_TRUE(diagnostics_tail(0).empty());

	const uint64_t events_before = etw_diagnostic_events_emitted();
	const uint64_t failures_before = etw_write_failures();

	for (uint64_t i = 0; i < 1000; ++i) {
		record_diagnostic(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedToken,
			i * 16,
			static_cast<uint32_t>(i + 1),
			0);
	}

	EXPECT_EQ(diagnostics_total_recorded(), 1000u);
	EXPECT_EQ(etw_diagnostic_events_emitted() - events_before, 1000u);
	EXPECT_EQ(etw_write_failures(), failures_before); // Windows accepted every write

	const auto full_tail = diagnostics_tail();
	ASSERT_EQ(full_tail.size(), kDiagnosticRingCapacity);
	for (size_t idx = 0; idx < kDiagnosticRingCapacity; ++idx) {
		const uint64_t expected_i = 1000 - kDiagnosticRingCapacity + idx;
		EXPECT_EQ(full_tail[idx].sequence, expected_i + 1);
		EXPECT_EQ(full_tail[idx].diagnostic_id, DiagnosticId::DamagedToken);
		EXPECT_EQ(full_tail[idx].message, diagnostic_id_message(DiagnosticId::DamagedToken));
		EXPECT_EQ(full_tail[idx].byte_offset, std::optional<uint64_t>(expected_i * 16));
		EXPECT_EQ(
			full_tail[idx].object_num,
			std::optional<uint32_t>(static_cast<uint32_t>(expected_i + 1)));
		EXPECT_GT(full_tail[idx].timestamp_utc_ms, 0u);
		EXPECT_GT(full_tail[idx].thread_id, 0u);
		if (idx > 0) {
			// Ordered by sequence; timestamp_utc_ms is wall-clock time, which can be set back.
			EXPECT_LT(full_tail[idx - 1].sequence, full_tail[idx].sequence);
		}
	}

	const auto short_tail = diagnostics_tail(10);
	ASSERT_EQ(short_tail.size(), 10u);
	for (size_t idx = 0; idx < 10; ++idx) {
		const uint64_t expected_i = 990 + idx;
		EXPECT_EQ(short_tail[idx].sequence, expected_i + 1);
		EXPECT_EQ(short_tail[idx].byte_offset, std::optional<uint64_t>(expected_i * 16));
	}

	// Freeze check: caller-supplied arbitrary strings (such as synthetic
	// document text or binary bytes) are never retained in the diagnostic ring.
	record_diagnostic(ErrorCode::DamagedFormat, "synthetic document text", 8192, 99, 3);
	const auto latest = diagnostics_tail(1);
	ASSERT_EQ(latest.size(), 1u);
	EXPECT_EQ(latest[0].diagnostic_id, DiagnosticId::DamagedFormat);
	EXPECT_EQ(latest[0].message, diagnostic_id_message(DiagnosticId::DamagedFormat));
	EXPECT_EQ(latest[0].message.find("synthetic"), std::string::npos);
	EXPECT_EQ(latest[0].format().find("synthetic"), std::string::npos);
	EXPECT_NE(latest[0].format().find("(offset=8192, obj=99 3 R)"), std::string::npos);

	clear_diagnostics();
	EXPECT_EQ(diagnostics_total_recorded(), 0u);
	EXPECT_TRUE(diagnostics_tail().empty());
}

TEST(PdfError, ConcurrentDiagnosticRecording)
{
	clear_diagnostics();

	constexpr size_t kThreadCount = 8;
	constexpr size_t kOpsPerThread = 1250; // 8 * 1250 = 10,000 total writes/reads
	std::atomic<bool> start_flag{false};
	std::atomic<size_t> ready_count{0};
	std::atomic<bool> ordering_ok{true};

	std::vector<std::thread> workers;
	workers.reserve(kThreadCount);
	for (size_t t = 0; t < kThreadCount; ++t) {
		workers.emplace_back([&, t]() {
			ready_count.fetch_add(1, std::memory_order_release);
			while (!start_flag.load(std::memory_order_acquire)) {
				std::this_thread::yield();
			}
			for (size_t i = 0; i < kOpsPerThread; ++i) {
				record_diagnostic(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedObject,
					static_cast<uint64_t>(t * kOpsPerThread + i),
					static_cast<uint32_t>(i + 1),
					static_cast<uint16_t>(t));
				if ((i & 7u) == 0u) {
					const auto snap = diagnostics_tail(32);
					for (size_t k = 1; k < snap.size(); ++k) {
						if (snap[k - 1].sequence >= snap[k].sequence) {
							ordering_ok.store(false, std::memory_order_relaxed);
						}
					}
				}
			}
		});
	}

	while (ready_count.load(std::memory_order_acquire) < kThreadCount) {
		std::this_thread::yield();
	}
	start_flag.store(true, std::memory_order_release);

	for (auto& w : workers) {
		w.join();
	}

	EXPECT_TRUE(ordering_ok.load());
	EXPECT_EQ(diagnostics_total_recorded(), kThreadCount * kOpsPerThread);
	const auto final_tail = diagnostics_tail();
	ASSERT_EQ(final_tail.size(), kDiagnosticRingCapacity);
	for (size_t idx = 0; idx < kDiagnosticRingCapacity; ++idx) {
		EXPECT_EQ(
			final_tail[idx].sequence,
			(kThreadCount * kOpsPerThread) - kDiagnosticRingCapacity + idx + 1);
	}
}

TEST(PdfError, EtwTraceLoggingProviderAndSpans)
{
	// Counted events are the ones written, which needs an enabled provider; no session listens in tests.
	force_enable_etw_provider_for_testing();
	clear_diagnostics();
	EXPECT_TRUE(ensure_etw_provider_registered());
	EXPECT_TRUE(is_etw_provider_registered());
	EXPECT_EQ(etw_provider_name(), std::string_view("Recto.Diagnostics"));

	// Capture actual EVENT_DATA_DESCRIPTOR packets emitted via TraceLoggingWrite
	// and verify provider metadata, event names, field names, scalar values,
	// temporary phase-name lifetime safety, and zero document byte payloads.
	clear_captured_etw_events_for_testing();
	set_etw_capture_for_testing(true);

	const uint64_t spans_before = etw_span_events_emitted();
	const uint64_t span_failures_before = etw_write_failures();
	{
		// Construct with a temporary std::string to prove EtwSpan resolves to a
		// closed EtwPhaseId across the scope until ~EtwSpan() emits SpanEnd.
		EtwSpan span(std::string("OpenDocument").c_str(), 42, 7);
		EXPECT_EQ(span.phase_id(), EtwPhaseId::OpenDocument);
		EXPECT_EQ(span.phase_name(), "OpenDocument");
		EXPECT_GE(span.elapsed_us(), 0u);
		record_diagnostic(ErrorCode::DamagedFormat, "synthetic document text", 2048, 19, 2);
	}
	{
		// Both free-form and identifier-shaped sensitive strings map to UnspecifiedPhase.
		EtwSpan unsafe_span("ConfidentialAccount123", 9, 1);
		EXPECT_EQ(unsafe_span.phase_id(), EtwPhaseId::UnspecifiedPhase);
		EXPECT_EQ(unsafe_span.phase_name(), "UnspecifiedPhase");
	}
	set_etw_capture_for_testing(false);

	EXPECT_EQ(etw_span_events_emitted() - spans_before, 4u);
	EXPECT_EQ(etw_write_failures(), span_failures_before); // Windows accepted every write
	const auto captured = captured_etw_events_for_testing();
	ASSERT_EQ(captured.size(), 5u);

	// 1. SpanStart for OpenDocument
	EXPECT_EQ(captured[0].provider_name, "Recto.Diagnostics");
	EXPECT_EQ(captured[0].event_name, "SpanStart");
	EXPECT_EQ(captured[0].level, WINEVENT_LEVEL_VERBOSE);
	const std::vector<std::string> expected_start_fields = {
		"Phase", "DocumentId", "PageIndex", "ThreadId"};
	EXPECT_EQ(captured[0].field_names, expected_start_fields);
	EXPECT_EQ(captured[0].phase, "OpenDocument");
	EXPECT_EQ(captured[0].document_id, 42u);
	EXPECT_EQ(captured[0].page_index, 7u);
	EXPECT_GT(captured[0].thread_id, 0u);

	// 2. Diagnostic inside the span
	EXPECT_EQ(captured[1].provider_name, "Recto.Diagnostics");
	EXPECT_EQ(captured[1].event_name, "Diagnostic");
	EXPECT_EQ(captured[1].level, WINEVENT_LEVEL_ERROR); // an Error-level session collects every diagnostic
	const std::vector<std::string> expected_diag_fields = {
		"Sequence",
		"ErrorCode",
		"ErrorCodeName",
		"DiagnosticId",
		"DiagnosticIdName",
		"HasByteOffset",
		"ByteOffset",
		"HasObjectNum",
		"ObjectNum",
		"GenerationNum",
		"ThreadId"};
	EXPECT_EQ(captured[1].field_names, expected_diag_fields);
	EXPECT_EQ(captured[1].sequence, 1u);
	EXPECT_EQ(captured[1].error_code, static_cast<uint32_t>(ErrorCode::DamagedFormat));
	EXPECT_EQ(captured[1].error_code_name, "DamagedFormat");
	EXPECT_EQ(captured[1].diagnostic_id, static_cast<uint16_t>(DiagnosticId::DamagedFormat));
	EXPECT_EQ(captured[1].diagnostic_id_name, "DamagedFormat");
	EXPECT_TRUE(captured[1].has_byte_offset);
	EXPECT_EQ(captured[1].byte_offset, 2048u);
	EXPECT_TRUE(captured[1].has_object_num);
	EXPECT_EQ(captured[1].object_num, 19u);
	EXPECT_EQ(captured[1].generation_num, 2u);
	EXPECT_GT(captured[1].thread_id, 0u);

	// 3. SpanEnd for OpenDocument (proving non-dangling phase name at destruction)
	EXPECT_EQ(captured[2].provider_name, "Recto.Diagnostics");
	EXPECT_EQ(captured[2].event_name, "SpanEnd");
	const std::vector<std::string> expected_end_fields = {
		"Phase", "DurationUs", "DocumentId", "PageIndex", "ThreadId"};
	EXPECT_EQ(captured[2].field_names, expected_end_fields);
	EXPECT_EQ(captured[2].phase, "OpenDocument");
	EXPECT_EQ(captured[2].document_id, 42u);
	EXPECT_EQ(captured[2].page_index, 7u);
	EXPECT_EQ(captured[2].thread_id, captured[0].thread_id);

	// 4 & 5. Sanitized SpanStart / SpanEnd for ConfidentialAccount123
	EXPECT_EQ(captured[3].event_name, "SpanStart");
	EXPECT_EQ(captured[3].phase, "UnspecifiedPhase");
	EXPECT_EQ(captured[4].event_name, "SpanEnd");
	EXPECT_EQ(captured[4].phase, "UnspecifiedPhase");
	clear_captured_etw_events_for_testing();

	constexpr size_t kThreadCount = 8;
	constexpr size_t kOpsPerThread = 1250; // 8 * 1250 = 10,000 concurrent spans & diagnostics
	const uint64_t total_spans_start = etw_span_events_emitted();
	const uint64_t total_diags_start = etw_diagnostic_events_emitted();
	const uint64_t total_failures_start = etw_write_failures();

	std::atomic<bool> start_flag{false};
	std::atomic<size_t> ready_count{0};
	std::vector<std::thread> workers;
	workers.reserve(kThreadCount);

	for (size_t t = 0; t < kThreadCount; ++t) {
		workers.emplace_back([&, t]() {
			ready_count.fetch_add(1, std::memory_order_release);
			while (!start_flag.load(std::memory_order_acquire)) {
				std::this_thread::yield();
			}
			for (size_t i = 0; i < kOpsPerThread; ++i) {
				RECTO_ETW_SPAN_DOC("RenderTile", static_cast<uint64_t>(t + 1), static_cast<uint32_t>(i));
				RECTO_ETW_SPAN("DecodeFilter");
				record_diagnostic(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					static_cast<uint64_t>(i),
					static_cast<uint32_t>(t + 1),
					0);
				if ((i & 15u) == 0u) {
					(void)diagnostics_tail(16);
				}
			}
		});
	}

	while (ready_count.load(std::memory_order_acquire) < kThreadCount) {
		std::this_thread::yield();
	}
	start_flag.store(true, std::memory_order_release);

	for (auto& w : workers) {
		w.join();
	}

	const uint64_t expected_span_events = kThreadCount * kOpsPerThread * 4u;
	const uint64_t expected_diag_events = kThreadCount * kOpsPerThread;
	EXPECT_EQ(etw_span_events_emitted() - total_spans_start, expected_span_events);
	EXPECT_EQ(etw_diagnostic_events_emitted() - total_diags_start, expected_diag_events);
	EXPECT_EQ(etw_write_failures(), total_failures_start); // Windows accepted every concurrent write
}

} // namespace
} // namespace recto::pdf
