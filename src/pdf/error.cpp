// Recto PDF engine error-handling and D16 diagnostic implementation (D01 T01 section 1).
#include "error.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <TraceLoggingProvider.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

TRACELOGGING_DEFINE_PROVIDER(
	g_recto_etw_provider,
	"Recto.Diagnostics",
	// {8c4e7f92-3b1a-4d5e-9a21-7c6b5d4e3f10}
	(0x8c4e7f92, 0x3b1a, 0x4d5e, 0x9a, 0x21, 0x7c, 0x6b, 0x5d, 0x4e, 0x3f, 0x10));

namespace recto::pdf {

namespace detail {

[[noreturn]] void fail_fast_invalid_result_access() noexcept
{
	__fastfail(FAST_FAIL_INVALID_ARG);
}

} // namespace detail

namespace {

struct DiagnosticSlot {
	uint64_t sequence{0};
	uint64_t timestamp_utc_ms{0};
	uint32_t thread_id{0};
	ErrorCode code{ErrorCode::Ok};
	bool has_byte_offset{false};
	uint64_t byte_offset{0};
	bool has_object_num{false};
	uint32_t object_num{0};
	uint16_t generation_num{0};
	uint16_t message_len{0};
	std::array<char, kMaxDiagnosticMessageBytes + 1> message{};
};

SRWLOCK g_ring_lock = SRWLOCK_INIT;
std::array<DiagnosticSlot, kDiagnosticRingCapacity> g_ring_slots{};
size_t g_ring_write_index{0};
size_t g_ring_count{0};
uint64_t g_ring_next_sequence{1};

SRWLOCK g_etw_lock = SRWLOCK_INIT;
std::atomic<bool> g_etw_registered{false};
std::atomic<uint64_t> g_etw_diagnostic_events{0};
std::atomic<uint64_t> g_etw_span_events{0};

struct EtwProviderUnregistrar {
	~EtwProviderUnregistrar() noexcept
	{
		AcquireSRWLockExclusive(&g_etw_lock);
		if (g_etw_registered.load(std::memory_order_relaxed)) {
			TraceLoggingUnregister(g_recto_etw_provider);
			g_etw_registered.store(false, std::memory_order_release);
		}
		ReleaseSRWLockExclusive(&g_etw_lock);
	}
};

uint64_t current_utc_timestamp_ms() noexcept
{
	FILETIME ft{};
	GetSystemTimePreciseAsFileTime(&ft);
	ULARGE_INTEGER uli{};
	uli.LowPart = ft.dwLowDateTime;
	uli.HighPart = ft.dwHighDateTime;
	// Windows FILETIME epoch is 1601-01-01; Unix epoch is 1970-01-01
	// (116444736000000000 100-ns intervals).
	constexpr uint64_t kUnixEpochDiff100Ns = 116444736000000000ULL;
	if (uli.QuadPart <= kUnixEpochDiff100Ns) {
		return 0;
	}
	return (uli.QuadPart - kUnixEpochDiff100Ns) / 10000ULL;
}

int64_t qpc_now() noexcept
{
	LARGE_INTEGER li{};
	QueryPerformanceCounter(&li);
	return li.QuadPart;
}

int64_t qpc_frequency() noexcept
{
	static const int64_t freq = []() noexcept {
		LARGE_INTEGER li{};
		QueryPerformanceFrequency(&li);
		return li.QuadPart > 0 ? li.QuadPart : 1;
	}();
	return freq;
}

uint64_t qpc_delta_to_us(int64_t start_qpc, int64_t end_qpc) noexcept
{
	if (end_qpc <= start_qpc) {
		return 0;
	}
	const uint64_t ticks = static_cast<uint64_t>(end_qpc - start_qpc);
	const uint64_t freq = static_cast<uint64_t>(qpc_frequency());
	const uint64_t whole_seconds = ticks / freq;
	const uint64_t rem_ticks = ticks % freq;
	return whole_seconds * 1'000'000ULL + (rem_ticks * 1'000'000ULL) / freq;
}

void append_context_suffix(
	std::string& out,
	const std::optional<uint64_t>& byte_offset,
	const std::optional<uint32_t>& object_num,
	uint16_t generation_num)
{
	if (!byte_offset.has_value() && !object_num.has_value()) {
		return;
	}
	out += " (";
	if (byte_offset.has_value()) {
		out += "offset=";
		out += std::to_string(*byte_offset);
		if (object_num.has_value()) {
			out += ", ";
		}
	}
	if (object_num.has_value()) {
		out += "obj=";
		out += std::to_string(*object_num);
		out += ' ';
		out += std::to_string(generation_num);
		out += " R";
	}
	out += ')';
}

const char* error_code_cstr(ErrorCode code) noexcept
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

} // namespace

std::string PdfError::format() const
{
	std::string out;
	const std::string_view code_str = error_code_name(code);
	out.reserve(code_str.size() + message.size() + 48);
	out += '[';
	out += code_str;
	out += ']';
	if (!message.empty()) {
		out += ' ';
		out += message;
	}
	append_context_suffix(out, byte_offset, object_num, generation_num);
	return out;
}

std::string DiagnosticEntry::format() const
{
	std::string out;
	const std::string_view code_str = error_code_name(code);
	out.reserve(code_str.size() + message.size() + 80);
	out += '#';
	out += std::to_string(sequence);
	out += " [t=";
	out += std::to_string(timestamp_utc_ms);
	out += " tid=";
	out += std::to_string(thread_id);
	out += " ";
	out += code_str;
	out += ']';
	if (!message.empty()) {
		out += ' ';
		out += message;
	}
	append_context_suffix(out, byte_offset, object_num, generation_num);
	return out;
}

bool ensure_etw_provider_registered() noexcept
{
	if (g_etw_registered.load(std::memory_order_acquire)) {
		return true;
	}
	AcquireSRWLockExclusive(&g_etw_lock);
	if (!g_etw_registered.load(std::memory_order_relaxed)) {
		static EtwProviderUnregistrar unregistrar{};
		const HRESULT hr = TraceLoggingRegister(g_recto_etw_provider);
		if (SUCCEEDED(hr)) {
			g_etw_registered.store(true, std::memory_order_release);
		}
	}
	const bool registered = g_etw_registered.load(std::memory_order_relaxed);
	ReleaseSRWLockExclusive(&g_etw_lock);
	return registered;
}

bool is_etw_provider_registered() noexcept
{
	return g_etw_registered.load(std::memory_order_acquire);
}

std::string_view etw_provider_name() noexcept
{
	return "Recto.Diagnostics";
}

uint64_t etw_diagnostic_events_emitted() noexcept
{
	return g_etw_diagnostic_events.load(std::memory_order_relaxed);
}

uint64_t etw_span_events_emitted() noexcept
{
	return g_etw_span_events.load(std::memory_order_relaxed);
}

void record_diagnostic(
	ErrorCode code,
	std::string_view message,
	std::optional<uint64_t> byte_offset,
	std::optional<uint32_t> object_num,
	uint16_t generation_num) noexcept
{
	ensure_etw_provider_registered();

	const uint64_t ts_ms = current_utc_timestamp_ms();
	const uint32_t tid = static_cast<uint32_t>(GetCurrentThreadId());
	const size_t copy_len = std::min(message.size(), kMaxDiagnosticMessageBytes);

	uint64_t assigned_seq = 0;
	{
		AcquireSRWLockExclusive(&g_ring_lock);
		DiagnosticSlot& slot = g_ring_slots[g_ring_write_index];
		assigned_seq = g_ring_next_sequence++;
		slot.sequence = assigned_seq;
		slot.timestamp_utc_ms = ts_ms;
		slot.thread_id = tid;
		slot.code = code;
		slot.has_byte_offset = byte_offset.has_value();
		slot.byte_offset = byte_offset.value_or(0);
		slot.has_object_num = object_num.has_value();
		slot.object_num = object_num.value_or(0);
		slot.generation_num = generation_num;
		slot.message_len = static_cast<uint16_t>(copy_len);
		for (size_t i = 0; i < copy_len; ++i) {
			const unsigned char ch = static_cast<unsigned char>(message[i]);
			slot.message[i] = (ch < 0x20u || ch == 0x7Fu) ? '?' : static_cast<char>(ch);
		}
		slot.message[copy_len] = '\0';

		g_ring_write_index = (g_ring_write_index + 1) % kDiagnosticRingCapacity;
		if (g_ring_count < kDiagnosticRingCapacity) {
			++g_ring_count;
		}
		ReleaseSRWLockExclusive(&g_ring_lock);
	}

	const char* const code_name_str = error_code_cstr(code);
	const uint32_t code_u32 = static_cast<uint32_t>(code);
	const bool has_off = byte_offset.has_value();
	const uint64_t off_val = byte_offset.value_or(0);
	const bool has_obj = object_num.has_value();
	const uint32_t obj_val = object_num.value_or(0);

	TraceLoggingWrite(
		g_recto_etw_provider,
		"Diagnostic",
		TraceLoggingUInt64(assigned_seq, "Sequence"),
		TraceLoggingUInt32(code_u32, "ErrorCode"),
		TraceLoggingString(code_name_str, "ErrorCodeName"),
		TraceLoggingBool(has_off, "HasByteOffset"),
		TraceLoggingUInt64(off_val, "ByteOffset"),
		TraceLoggingBool(has_obj, "HasObjectNum"),
		TraceLoggingUInt32(obj_val, "ObjectNum"),
		TraceLoggingUInt16(generation_num, "GenerationNum"),
		TraceLoggingUInt32(tid, "ThreadId"));
	g_etw_diagnostic_events.fetch_add(1, std::memory_order_relaxed);
}

void record_diagnostic(const PdfError& err) noexcept
{
	record_diagnostic(
		err.code,
		err.message,
		err.byte_offset,
		err.object_num,
		err.generation_num);
}

std::vector<DiagnosticEntry> diagnostics_tail(size_t max_entries)
{
	if (max_entries == 0) {
		return {};
	}

	std::array<DiagnosticSlot, kDiagnosticRingCapacity> snapshot{};
	size_t count = 0;
	{
		AcquireSRWLockShared(&g_ring_lock);
		count = std::min(max_entries, g_ring_count);
		if (count > 0) {
			// Oldest among the `count` newest entries starts `count` slots
			// behind `g_ring_write_index` (modulo kDiagnosticRingCapacity).
			const size_t start_index =
				(g_ring_write_index + kDiagnosticRingCapacity - count) % kDiagnosticRingCapacity;
			for (size_t i = 0; i < count; ++i) {
				snapshot[i] = g_ring_slots[(start_index + i) % kDiagnosticRingCapacity];
			}
		}
		ReleaseSRWLockShared(&g_ring_lock);
	}

	std::vector<DiagnosticEntry> result;
	result.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		const DiagnosticSlot& slot = snapshot[i];
		DiagnosticEntry entry{};
		entry.sequence = slot.sequence;
		entry.timestamp_utc_ms = slot.timestamp_utc_ms;
		entry.thread_id = slot.thread_id;
		entry.code = slot.code;
		entry.message.assign(slot.message.data(), slot.message_len);
		if (slot.has_byte_offset) {
			entry.byte_offset = slot.byte_offset;
		}
		if (slot.has_object_num) {
			entry.object_num = slot.object_num;
		}
		entry.generation_num = slot.generation_num;
		result.push_back(std::move(entry));
	}
	return result;
}

void clear_diagnostics() noexcept
{
	AcquireSRWLockExclusive(&g_ring_lock);
	g_ring_slots.fill(DiagnosticSlot{});
	g_ring_write_index = 0;
	g_ring_count = 0;
	g_ring_next_sequence = 1;
	ReleaseSRWLockExclusive(&g_ring_lock);
}

uint64_t diagnostics_total_recorded() noexcept
{
	AcquireSRWLockShared(&g_ring_lock);
	const uint64_t total = g_ring_next_sequence - 1;
	ReleaseSRWLockShared(&g_ring_lock);
	return total;
}

PdfError make_pdf_error(
	ErrorCode code,
	std::string message,
	std::optional<uint64_t> byte_offset,
	std::optional<uint32_t> object_num,
	uint16_t generation_num)
{
	PdfError err{
		code,
		std::move(message),
		byte_offset,
		object_num,
		generation_num};
	if (should_auto_record_diagnostic(code)) {
		record_diagnostic(err);
	}
	return err;
}

EtwSpan::EtwSpan(
	const char* phase_name,
	uint64_t document_id,
	uint32_t page_index) noexcept
	: phase_name_(phase_name ? phase_name : "")
	, document_id_(document_id)
	, page_index_(page_index)
	, thread_id_(static_cast<uint32_t>(GetCurrentThreadId()))
	, start_qpc_(qpc_now())
{
	ensure_etw_provider_registered();
	TraceLoggingWrite(
		g_recto_etw_provider,
		"SpanStart",
		TraceLoggingString(phase_name_, "Phase"),
		TraceLoggingUInt64(document_id_, "DocumentId"),
		TraceLoggingUInt32(page_index_, "PageIndex"),
		TraceLoggingUInt32(thread_id_, "ThreadId"));
	g_etw_span_events.fetch_add(1, std::memory_order_relaxed);
}

EtwSpan::~EtwSpan() noexcept
{
	const uint64_t duration_us = elapsed_us();
	TraceLoggingWrite(
		g_recto_etw_provider,
		"SpanEnd",
		TraceLoggingString(phase_name_, "Phase"),
		TraceLoggingUInt64(duration_us, "DurationUs"),
		TraceLoggingUInt64(document_id_, "DocumentId"),
		TraceLoggingUInt32(page_index_, "PageIndex"),
		TraceLoggingUInt32(thread_id_, "ThreadId"));
	g_etw_span_events.fetch_add(1, std::memory_order_relaxed);
}

uint64_t EtwSpan::elapsed_us() const noexcept
{
	return qpc_delta_to_us(start_qpc_, qpc_now());
}

} // namespace recto::pdf
