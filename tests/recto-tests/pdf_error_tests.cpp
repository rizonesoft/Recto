// Tests for PdfResult<T>, PdfError, ErrorCode, D16 diagnostic ring buffer,
// and ETW TraceLogging provider/spans (D01 T01 section 1).
#include "error.h"
#include "test_support.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <thread>
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

TEST(PdfError, ResultValueAndError)
{
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

	// Automatic diagnostic recording on make_pdf_error for DamagedFormat,
	// DecompressionLimitExceeded, and RecursionLimitExceeded.
	clear_diagnostics();
	const uint64_t diag_events_before = etw_diagnostic_events_emitted();

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
	EXPECT_EQ(tail[0].message, "bad dictionary");
	EXPECT_EQ(tail[0].byte_offset, std::optional<uint64_t>(128));
	EXPECT_EQ(tail[0].object_num, std::optional<uint32_t>(4));
	EXPECT_EQ(tail[0].generation_num, 0u);

	EXPECT_EQ(tail[1].code, auto2.code);
	EXPECT_EQ(tail[1].message, "stream exceeds 256 MB");
	EXPECT_EQ(tail[1].byte_offset, std::optional<uint64_t>(512));
	EXPECT_EQ(tail[1].object_num, std::optional<uint32_t>(9));

	EXPECT_EQ(tail[2].code, auto3.code);
	EXPECT_EQ(tail[2].message, "nesting depth > 64");
	EXPECT_EQ(tail[2].byte_offset, std::optional<uint64_t>(768));
	EXPECT_EQ(tail[2].object_num, std::optional<uint32_t>(11));
	EXPECT_EQ(tail[2].generation_num, 1u);

	EXPECT_GE(etw_diagnostic_events_emitted(), diag_events_before + 3);
}

TEST(PdfError, DiagnosticRingWraparound)
{
	clear_diagnostics();
	EXPECT_EQ(diagnostics_total_recorded(), 0u);
	EXPECT_TRUE(diagnostics_tail().empty());
	EXPECT_TRUE(diagnostics_tail(0).empty());

	const uint64_t events_before = etw_diagnostic_events_emitted();

	for (uint64_t i = 0; i < 1000; ++i) {
		record_diagnostic(
			ErrorCode::DamagedFormat,
			"entry-" + std::to_string(i),
			i * 16,
			static_cast<uint32_t>(i + 1),
			0);
	}

	EXPECT_EQ(diagnostics_total_recorded(), 1000u);
	EXPECT_EQ(etw_diagnostic_events_emitted() - events_before, 1000u);

	const auto full_tail = diagnostics_tail();
	ASSERT_EQ(full_tail.size(), kDiagnosticRingCapacity);
	for (size_t idx = 0; idx < kDiagnosticRingCapacity; ++idx) {
		const uint64_t expected_i = 1000 - kDiagnosticRingCapacity + idx;
		EXPECT_EQ(full_tail[idx].sequence, expected_i + 1);
		EXPECT_EQ(full_tail[idx].message, "entry-" + std::to_string(expected_i));
		EXPECT_EQ(full_tail[idx].byte_offset, std::optional<uint64_t>(expected_i * 16));
		EXPECT_EQ(
			full_tail[idx].object_num,
			std::optional<uint32_t>(static_cast<uint32_t>(expected_i + 1)));
		EXPECT_GT(full_tail[idx].timestamp_utc_ms, 0u);
		EXPECT_GT(full_tail[idx].thread_id, 0u);
		if (idx > 0) {
			EXPECT_LT(full_tail[idx - 1].sequence, full_tail[idx].sequence);
			EXPECT_LE(full_tail[idx - 1].timestamp_utc_ms, full_tail[idx].timestamp_utc_ms);
		}
	}

	const auto short_tail = diagnostics_tail(10);
	ASSERT_EQ(short_tail.size(), 10u);
	for (size_t idx = 0; idx < 10; ++idx) {
		const uint64_t expected_i = 990 + idx;
		EXPECT_EQ(short_tail[idx].sequence, expected_i + 1);
		EXPECT_EQ(short_tail[idx].message, "entry-" + std::to_string(expected_i));
	}

	// Verify message truncation and control-byte sanitization.
	std::string overlong(400, 'A');
	overlong[0] = '\x00';
	overlong[1] = '\x1F';
	overlong[2] = '\x7F';
	record_diagnostic(ErrorCode::DamagedFormat, overlong, 8192, 99, 3);
	const auto latest = diagnostics_tail(1);
	ASSERT_EQ(latest.size(), 1u);
	EXPECT_EQ(latest[0].message.size(), kMaxDiagnosticMessageBytes);
	EXPECT_EQ(latest[0].message.substr(0, 4), "???A");
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
					"concurrent-diag",
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
	EXPECT_TRUE(ensure_etw_provider_registered());
	EXPECT_TRUE(is_etw_provider_registered());
	EXPECT_EQ(etw_provider_name(), std::string_view("Recto.Diagnostics"));

	const uint64_t spans_before = etw_span_events_emitted();
	{
		EtwSpan span("OpenDocument", 42, 0);
		EXPECT_GE(span.elapsed_us(), 0u);
	}
	EXPECT_EQ(etw_span_events_emitted() - spans_before, 2u);

	constexpr size_t kThreadCount = 8;
	constexpr size_t kOpsPerThread = 1250; // 8 * 1250 = 10,000 concurrent spans & diagnostics
	const uint64_t total_spans_start = etw_span_events_emitted();
	const uint64_t total_diags_start = etw_diagnostic_events_emitted();

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
					"etw-span-test",
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
}

} // namespace
} // namespace recto::pdf
