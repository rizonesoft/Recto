// Recto PDF engine error-handling and D16 diagnostic implementation (D01 T01 section 1).
#include "error.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <evntprov.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

extern "C" ULONG WINAPI RectoTlgEventWriteTransfer(
	REGHANDLE RegHandle,
	 PCEVENT_DESCRIPTOR EventDescriptor,
	LPCGUID ActivityId,
	LPCGUID RelatedActivityId,
	ULONG UserDataCount,
	PEVENT_DATA_DESCRIPTOR UserData) noexcept;

#define TLG_EVENT_WRITE_TRANSFER RectoTlgEventWriteTransfer
#include <TraceLoggingProvider.h>

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

// Fixed-size slot in the 256-entry ring buffer. Contains only numeric fields
// and closed enumerators (ErrorCode + DiagnosticId); zero caller-supplied byte
// buffers exist in DiagnosticSlot.
struct DiagnosticSlot {
	uint64_t sequence{0};
	uint64_t timestamp_utc_ms{0};
	uint32_t thread_id{0};
	ErrorCode code{ErrorCode::Ok};
	DiagnosticId diagnostic_id{DiagnosticId::GenericStatus};
	bool has_byte_offset{false};
	uint64_t byte_offset{0};
	bool has_object_num{false};
	uint32_t object_num{0};
	uint16_t generation_num{0};
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

SRWLOCK g_etw_capture_lock = SRWLOCK_INIT;
std::atomic<bool> g_etw_capture_enabled{false};
constexpr size_t kMaxCapturedEtwEvents = 512;

std::vector<CapturedEtwEvent>& captured_etw_storage()
{
	static std::vector<CapturedEtwEvent> events;
	return events;
}

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

const char* diagnostic_id_cstr(DiagnosticId id) noexcept
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

bool ascii_iequals(std::string_view a, std::string_view b) noexcept
{
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); ++i) {
		unsigned char ca = static_cast<unsigned char>(a[i]);
		unsigned char cb = static_cast<unsigned char>(b[i]);
		if (ca >= 'A' && ca <= 'Z') {
			ca = static_cast<unsigned char>(ca - 'A' + 'a');
		}
		if (cb >= 'A' && cb <= 'Z') {
			cb = static_cast<unsigned char>(cb - 'A' + 'a');
		}
		if (ca != cb) {
			return false;
		}
	}
	return true;
}

bool is_valid_phase_identifier(std::string_view phase) noexcept
{
	if (phase.empty() || phase.size() > kMaxSpanPhaseNameBytes) {
		return false;
	}
	for (const char ch : phase) {
		const unsigned char u = static_cast<unsigned char>(ch);
		const bool ok =
			(u >= 'A' && u <= 'Z') ||
			(u >= 'a' && u <= 'z') ||
			(u >= '0' && u <= '9') ||
			u == '_' || u == '.' || u == ':' || u == '-';
		if (!ok) {
			return false;
		}
	}
	return true;
}

template <typename T>
T read_descriptor_scalar(const EVENT_DATA_DESCRIPTOR& desc) noexcept
{
	T val{};
	if (desc.Ptr != 0 && desc.Size >= sizeof(T)) {
		std::memcpy(&val, reinterpret_cast<const void*>(static_cast<uintptr_t>(desc.Ptr)), sizeof(T));
	}
	return val;
}

std::string read_descriptor_cstr(const EVENT_DATA_DESCRIPTOR& desc)
{
	if (desc.Ptr == 0 || desc.Size == 0) {
		return {};
	}
	const char* ptr = reinterpret_cast<const char*>(static_cast<uintptr_t>(desc.Ptr));
	size_t len = 0;
	while (len < desc.Size && ptr[len] != '\0') {
		++len;
	}
	return std::string(ptr, len);
}

std::string extract_provider_name(const EVENT_DATA_DESCRIPTOR& desc)
{
	if (desc.Ptr == 0 || desc.Size <= 2) {
		return {};
	}
	const uint8_t* bytes = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(desc.Ptr));
	size_t pos = 2; // Skip UINT16 total size
	size_t start = pos;
	while (pos < desc.Size && bytes[pos] != 0) {
		++pos;
	}
	return std::string(reinterpret_cast<const char*>(bytes + start), pos - start);
}

void parse_event_metadata(
	const EVENT_DATA_DESCRIPTOR& desc,
	std::string& out_event_name,
	std::vector<std::string>& out_field_names)
{
	out_event_name.clear();
	out_field_names.clear();
	if (desc.Ptr == 0 || desc.Size <= 3) {
		return;
	}
	const uint8_t* bytes = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(desc.Ptr));
	size_t pos = 2; // Skip UINT16 size
	// Skip variable-length event tags (high bit set means continuation byte).
	while (pos < desc.Size && (bytes[pos] & 0x80u) != 0) {
		++pos;
	}
	if (pos < desc.Size) {
		++pos;
	}
	const size_t name_start = pos;
	while (pos < desc.Size && bytes[pos] != 0) {
		++pos;
	}
	out_event_name.assign(reinterpret_cast<const char*>(bytes + name_start), pos - name_start);
	if (pos < desc.Size) {
		++pos; // Skip '\0'
	}

	while (pos < desc.Size) {
		const size_t field_start = pos;
		while (pos < desc.Size && bytes[pos] != 0) {
			++pos;
		}
		if (pos == field_start) {
			break;
		}
		out_field_names.emplace_back(
			reinterpret_cast<const char*>(bytes + field_start),
			pos - field_start);
		if (pos < desc.Size) {
			++pos; // Skip '\0'
		}
		if (pos >= desc.Size) {
			break;
		}
		const uint8_t in_type = bytes[pos++];
		if ((in_type & 0x80u) != 0 && pos < desc.Size) {
			const uint8_t out_type = bytes[pos++];
			// Skip optional field tags if continuation bit is set on out_type.
			if ((out_type & 0x80u) != 0) {
				while (pos < desc.Size && (bytes[pos] & 0x80u) != 0) {
					++pos;
				}
				if (pos < desc.Size) {
					++pos;
				}
			}
		}
	}
}

void process_tlg_write_transfer(
	ULONG user_data_count,
	PEVENT_DATA_DESCRIPTOR user_data) noexcept
{
	if (!user_data || user_data_count < 2) {
		return;
	}
	const uint8_t* meta =
		reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(user_data[1].Ptr));
	const size_t meta_size = user_data[1].Size;
	if (!meta || meta_size <= 3) {
		return;
	}
	size_t pos = 2;
	while (pos < meta_size && (meta[pos] & 0x80u) != 0) {
		++pos;
	}
	if (pos < meta_size) {
		++pos;
	}
	const char* ev_name = reinterpret_cast<const char*>(meta + pos);
	const size_t max_name_len = meta_size - pos;
	const size_t name_len = strnlen(ev_name, max_name_len);
	const std::string_view event_name_sv(ev_name, name_len);

	if (event_name_sv == "Diagnostic") {
		g_etw_diagnostic_events.fetch_add(1, std::memory_order_relaxed);
	} else if (event_name_sv == "SpanStart" || event_name_sv == "SpanEnd") {
		g_etw_span_events.fetch_add(1, std::memory_order_relaxed);
	}

	if (!g_etw_capture_enabled.load(std::memory_order_acquire)) {
		return;
	}

	try {
		CapturedEtwEvent ev{};
		ev.provider_name = extract_provider_name(user_data[0]);
		parse_event_metadata(user_data[1], ev.event_name, ev.field_names);

		if (ev.event_name == "Diagnostic" && user_data_count >= 13) {
			ev.sequence = read_descriptor_scalar<uint64_t>(user_data[2]);
			ev.error_code = read_descriptor_scalar<uint32_t>(user_data[3]);
			ev.error_code_name = read_descriptor_cstr(user_data[4]);
			ev.diagnostic_id = read_descriptor_scalar<uint16_t>(user_data[5]);
			ev.diagnostic_id_name = read_descriptor_cstr(user_data[6]);
			ev.has_byte_offset = read_descriptor_scalar<uint8_t>(user_data[7]) != 0;
			ev.byte_offset = read_descriptor_scalar<uint64_t>(user_data[8]);
			ev.has_object_num = read_descriptor_scalar<uint8_t>(user_data[9]) != 0;
			ev.object_num = read_descriptor_scalar<uint32_t>(user_data[10]);
			ev.generation_num = read_descriptor_scalar<uint16_t>(user_data[11]);
			ev.thread_id = read_descriptor_scalar<uint32_t>(user_data[12]);
		} else if (ev.event_name == "SpanStart" && user_data_count >= 6) {
			ev.phase = read_descriptor_cstr(user_data[2]);
			ev.document_id = read_descriptor_scalar<uint64_t>(user_data[3]);
			ev.page_index = read_descriptor_scalar<uint32_t>(user_data[4]);
			ev.thread_id = read_descriptor_scalar<uint32_t>(user_data[5]);
		} else if (ev.event_name == "SpanEnd" && user_data_count >= 7) {
			ev.phase = read_descriptor_cstr(user_data[2]);
			ev.duration_us = read_descriptor_scalar<uint64_t>(user_data[3]);
			ev.document_id = read_descriptor_scalar<uint64_t>(user_data[4]);
			ev.page_index = read_descriptor_scalar<uint32_t>(user_data[5]);
			ev.thread_id = read_descriptor_scalar<uint32_t>(user_data[6]);
		}

		AcquireSRWLockExclusive(&g_etw_capture_lock);
		auto& vec = captured_etw_storage();
		if (vec.size() < kMaxCapturedEtwEvents) {
			vec.push_back(std::move(ev));
		}
		ReleaseSRWLockExclusive(&g_etw_capture_lock);
	} catch (...) {
	}
}

} // namespace

} // namespace recto::pdf

extern "C" ULONG WINAPI RectoTlgEventWriteTransfer(
	REGHANDLE RegHandle,
	PCEVENT_DESCRIPTOR EventDescriptor,
	LPCGUID ActivityId,
	LPCGUID RelatedActivityId,
	ULONG UserDataCount,
	PEVENT_DATA_DESCRIPTOR UserData) noexcept
{
	recto::pdf::process_tlg_write_transfer(UserDataCount, UserData);
	return ::EventWriteTransfer(
		RegHandle,
		EventDescriptor,
		ActivityId,
		RelatedActivityId,
		UserDataCount,
		UserData);
}

namespace recto::pdf {

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
	const std::string_view diag_str = diagnostic_id_name(diagnostic_id);
	out.reserve(code_str.size() + diag_str.size() + message.size() + 96);
	out += '#';
	out += std::to_string(sequence);
	out += " [t=";
	out += std::to_string(timestamp_utc_ms);
	out += " tid=";
	out += std::to_string(thread_id);
	out += " ";
	out += code_str;
	out += '/';
	out += diag_str;
	out += ']';
	if (!message.empty()) {
		out += ' ';
		out += message;
	}
	append_context_suffix(out, byte_offset, object_num, generation_num);
	return out;
}

DiagnosticId classify_diagnostic_id(ErrorCode code, std::string_view hint) noexcept
{
	if (!hint.empty()) {
		if (ascii_iequals(hint, "DamagedHeader") || ascii_iequals(hint, "bad header")) {
			return DiagnosticId::DamagedHeader;
		}
		if (ascii_iequals(hint, "DamagedToken") || ascii_iequals(hint, "bad token")) {
			return DiagnosticId::DamagedToken;
		}
		if (ascii_iequals(hint, "DamagedObject") || ascii_iequals(hint, "bad object")) {
			return DiagnosticId::DamagedObject;
		}
		if (ascii_iequals(hint, "DamagedDictionary") || ascii_iequals(hint, "bad dictionary")) {
			return DiagnosticId::DamagedDictionary;
		}
		if (ascii_iequals(hint, "DamagedXRef") || ascii_iequals(hint, "bad xref")) {
			return DiagnosticId::DamagedXRef;
		}
		if (ascii_iequals(hint, "DamagedStream") || ascii_iequals(hint, "bad stream")) {
			return DiagnosticId::DamagedStream;
		}
		if (ascii_iequals(hint, "DamagedTrailer") || ascii_iequals(hint, "bad trailer")) {
			return DiagnosticId::DamagedTrailer;
		}
		if (ascii_iequals(hint, "RecoveredViaLinearScan") || ascii_iequals(hint, "linear scan")) {
			return DiagnosticId::RecoveredViaLinearScan;
		}
		if (ascii_iequals(hint, "BlockedLaunchAction") || ascii_iequals(hint, "launch")) {
			return DiagnosticId::BlockedLaunchAction;
		}
		if (ascii_iequals(hint, "BlockedJavaScriptAction") || ascii_iequals(hint, "javascript")) {
			return DiagnosticId::BlockedJavaScriptAction;
		}
		if (ascii_iequals(hint, "BlockedSubmitFormAction") || ascii_iequals(hint, "submitform")) {
			return DiagnosticId::BlockedSubmitFormAction;
		}
		if (ascii_iequals(hint, "BlockedImportDataAction") || ascii_iequals(hint, "importdata")) {
			return DiagnosticId::BlockedImportDataAction;
		}
		if (ascii_iequals(hint, "CorruptRecentsStore") || ascii_iequals(hint, "recents.json")) {
			return DiagnosticId::CorruptRecentsStore;
		}
		if (ascii_iequals(hint, "CorruptSettingsStore") || ascii_iequals(hint, "settings.json")) {
			return DiagnosticId::CorruptSettingsStore;
		}
	}
	return default_diagnostic_id(code);
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
			// Ensure TraceLoggingWrite always executes descriptor marshaling and
			// invokes TLG_EVENT_WRITE_TRANSFER even when no external kernel ETW
			// trace session has enabled the provider yet.
			if (g_recto_etw_provider->LevelPlus1 == 0) {
				const GUID provider_id = TraceLoggingProviderId(g_recto_etw_provider);
				_tlgEnableCallback(
					&provider_id,
					1u, // EVENT_CONTROL_CODE_ENABLE_PROVIDER
					5u, // WINEVENT_LEVEL_VERBOSE
					0ULL,
					0ULL,
					nullptr,
					const_cast<_tlgProvider_t*>(g_recto_etw_provider));
			}
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

void set_etw_capture_for_testing(bool enabled) noexcept
{
	ensure_etw_provider_registered();
	AcquireSRWLockExclusive(&g_etw_capture_lock);
	if (enabled) {
		try {
			captured_etw_storage().reserve(kMaxCapturedEtwEvents);
		} catch (...) {
		}
	}
	g_etw_capture_enabled.store(enabled, std::memory_order_release);
	ReleaseSRWLockExclusive(&g_etw_capture_lock);
}

void clear_captured_etw_events_for_testing() noexcept
{
	AcquireSRWLockExclusive(&g_etw_capture_lock);
	captured_etw_storage().clear();
	ReleaseSRWLockExclusive(&g_etw_capture_lock);
}

std::vector<CapturedEtwEvent> captured_etw_events_for_testing()
{
	AcquireSRWLockShared(&g_etw_capture_lock);
	std::vector<CapturedEtwEvent> copy = captured_etw_storage();
	ReleaseSRWLockShared(&g_etw_capture_lock);
	return copy;
}

void record_diagnostic(
	ErrorCode code,
	DiagnosticId diagnostic_id,
	std::optional<uint64_t> byte_offset,
	std::optional<uint32_t> object_num,
	uint16_t generation_num) noexcept
{
	ensure_etw_provider_registered();

	const uint64_t ts_ms = current_utc_timestamp_ms();
	const uint32_t tid = static_cast<uint32_t>(GetCurrentThreadId());

	uint64_t assigned_seq = 0;
	{
		AcquireSRWLockExclusive(&g_ring_lock);
		DiagnosticSlot& slot = g_ring_slots[g_ring_write_index];
		assigned_seq = g_ring_next_sequence++;
		slot.sequence = assigned_seq;
		slot.timestamp_utc_ms = ts_ms;
		slot.thread_id = tid;
		slot.code = code;
		slot.diagnostic_id = diagnostic_id;
		slot.has_byte_offset = byte_offset.has_value();
		slot.byte_offset = byte_offset.value_or(0);
		slot.has_object_num = object_num.has_value();
		slot.object_num = object_num.value_or(0);
		slot.generation_num = generation_num;

		g_ring_write_index = (g_ring_write_index + 1) % kDiagnosticRingCapacity;
		if (g_ring_count < kDiagnosticRingCapacity) {
			++g_ring_count;
		}
		ReleaseSRWLockExclusive(&g_ring_lock);
	}

	const char* const code_name_str = error_code_cstr(code);
	const uint32_t code_u32 = static_cast<uint32_t>(code);
	const uint16_t diag_u16 = static_cast<uint16_t>(diagnostic_id);
	const char* const diag_name_str = diagnostic_id_cstr(diagnostic_id);
	const uint8_t has_off = byte_offset.has_value() ? 1u : 0u;
	const uint64_t off_val = byte_offset.value_or(0);
	const uint8_t has_obj = object_num.has_value() ? 1u : 0u;
	const uint32_t obj_val = object_num.value_or(0);

	TraceLoggingWrite(
		g_recto_etw_provider,
		"Diagnostic",
		TraceLoggingUInt64(assigned_seq, "Sequence"),
		TraceLoggingUInt32(code_u32, "ErrorCode"),
		TraceLoggingString(code_name_str, "ErrorCodeName"),
		TraceLoggingUInt16(diag_u16, "DiagnosticId"),
		TraceLoggingString(diag_name_str, "DiagnosticIdName"),
		TraceLoggingUInt8(has_off, "HasByteOffset"),
		TraceLoggingUInt64(off_val, "ByteOffset"),
		TraceLoggingUInt8(has_obj, "HasObjectNum"),
		TraceLoggingUInt32(obj_val, "ObjectNum"),
		TraceLoggingUInt16(generation_num, "GenerationNum"),
		TraceLoggingUInt32(tid, "ThreadId"));
}

void record_diagnostic(
	ErrorCode code,
	std::string_view hint,
	std::optional<uint64_t> byte_offset,
	std::optional<uint32_t> object_num,
	uint16_t generation_num) noexcept
{
	record_diagnostic(
		code,
		classify_diagnostic_id(code, hint),
		byte_offset,
		object_num,
		generation_num);
}

void record_diagnostic(const PdfError& err) noexcept
{
	record_diagnostic(
		err.code,
		classify_diagnostic_id(err.code, err.message),
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
		entry.diagnostic_id = slot.diagnostic_id;
		entry.message = std::string(diagnostic_id_message(slot.diagnostic_id));
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
	std::string_view message,
	std::optional<uint64_t> byte_offset,
	std::optional<uint32_t> object_num,
	uint16_t generation_num) noexcept
{
	PdfError err(
		code,
		message,
		byte_offset,
		object_num,
		generation_num);
	if (should_auto_record_diagnostic(code)) {
		record_diagnostic(err);
	}
	return err;
}

EtwSpan::EtwSpan(
	std::string_view phase_name,
	uint64_t document_id,
	uint32_t page_index) noexcept
	: document_id_(document_id)
	, page_index_(page_index)
	, thread_id_(static_cast<uint32_t>(GetCurrentThreadId()))
	, start_qpc_(qpc_now())
{
	const std::string_view safe_phase =
		is_valid_phase_identifier(phase_name) ? phase_name : std::string_view("UnspecifiedPhase");
	const size_t copy_len = std::min(safe_phase.size(), kMaxSpanPhaseNameBytes);
	std::memcpy(phase_name_.data(), safe_phase.data(), copy_len);
	phase_name_[copy_len] = '\0';
	phase_len_ = static_cast<uint16_t>(copy_len);

	ensure_etw_provider_registered();
	TraceLoggingWrite(
		g_recto_etw_provider,
		"SpanStart",
		TraceLoggingString(phase_name_.data(), "Phase"),
		TraceLoggingUInt64(document_id_, "DocumentId"),
		TraceLoggingUInt32(page_index_, "PageIndex"),
		TraceLoggingUInt32(thread_id_, "ThreadId"));
}

EtwSpan::~EtwSpan() noexcept
{
	const uint64_t duration_us = elapsed_us();
	TraceLoggingWrite(
		g_recto_etw_provider,
		"SpanEnd",
		TraceLoggingString(phase_name_.data(), "Phase"),
		TraceLoggingUInt64(duration_us, "DurationUs"),
		TraceLoggingUInt64(document_id_, "DocumentId"),
		TraceLoggingUInt32(page_index_, "PageIndex"),
		TraceLoggingUInt32(thread_id_, "ThreadId"));
}

uint64_t EtwSpan::elapsed_us() const noexcept
{
	return qpc_delta_to_us(start_qpc_, qpc_now());
}

std::string_view EtwSpan::phase_name() const noexcept
{
	return std::string_view(phase_name_.data(), phase_len_);
}

} // namespace recto::pdf
