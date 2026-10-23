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
#include <winmeta.h>
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

class ScopedSrwLockExclusive {
public:
	explicit ScopedSrwLockExclusive(SRWLOCK& lock) noexcept
		: lock_(lock)
	{
		AcquireSRWLockExclusive(&lock_);
	}

	~ScopedSrwLockExclusive() noexcept
	{
		ReleaseSRWLockExclusive(&lock_);
	}

	ScopedSrwLockExclusive(const ScopedSrwLockExclusive&) = delete;
	ScopedSrwLockExclusive& operator=(const ScopedSrwLockExclusive&) = delete;

private:
	SRWLOCK& lock_;
};

class ScopedSrwLockShared {
public:
	explicit ScopedSrwLockShared(SRWLOCK& lock) noexcept
		: lock_(lock)
	{
		AcquireSRWLockShared(&lock_);
	}

	~ScopedSrwLockShared() noexcept
	{
		ReleaseSRWLockShared(&lock_);
	}

	ScopedSrwLockShared(const ScopedSrwLockShared&) = delete;
	ScopedSrwLockShared& operator=(const ScopedSrwLockShared&) = delete;

private:
	SRWLOCK& lock_;
};

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

constexpr size_t kMaxCapturedEtwEvents = 128;
constexpr size_t kMaxCapturedFields = 12;
constexpr size_t kMaxCapturedNameLen = 31;

struct CapturedEtwSlot {
	std::array<char, kMaxCapturedNameLen + 1> provider_name{};
	std::array<char, kMaxCapturedNameLen + 1> event_name{};
	uint8_t field_count{0};
	std::array<std::array<char, kMaxCapturedNameLen + 1>, kMaxCapturedFields> field_names{};
	uint8_t level{0};
	uint64_t sequence{0};
	uint32_t error_code{0};
	std::array<char, kMaxCapturedNameLen + 1> error_code_name{};
	uint16_t diagnostic_id{0};
	std::array<char, kMaxCapturedNameLen + 1> diagnostic_id_name{};
	bool has_byte_offset{false};
	uint64_t byte_offset{0};
	bool has_object_num{false};
	uint32_t object_num{0};
	uint16_t generation_num{0};
	std::array<char, kMaxCapturedNameLen + 1> phase{};
	uint64_t duration_us{0};
	uint64_t document_id{0};
	uint32_t page_index{0};
	uint32_t thread_id{0};
};

SRWLOCK g_etw_capture_lock = SRWLOCK_INIT;
std::atomic<bool> g_etw_capture_enabled{false};
std::array<CapturedEtwSlot, kMaxCapturedEtwEvents> g_etw_capture_slots{};
size_t g_etw_capture_count{0};

struct EtwProviderUnregistrar {
	~EtwProviderUnregistrar() noexcept
	{
		ScopedSrwLockExclusive lock(g_etw_lock);
		if (g_etw_registered.load(std::memory_order_relaxed)) {
			TraceLoggingUnregister(g_recto_etw_provider);
			g_etw_registered.store(false, std::memory_order_release);
		}
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

// ETW wants null-terminated names. Every name `error_code_name`, `diagnostic_id_name` and `etw_phase_name` return is a
// string literal, so their data is null-terminated and the tables in error.h stay the only ones.
const char* error_code_cstr(ErrorCode code) noexcept
{
	return error_code_name(code).data();
}

const char* diagnostic_id_cstr(DiagnosticId id) noexcept
{
	return diagnostic_id_name(id).data();
}

const char* etw_phase_cstr(EtwPhaseId phase_id) noexcept
{
	return etw_phase_name(phase_id).data();
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

template <typename T>
T read_descriptor_scalar(const EVENT_DATA_DESCRIPTOR& desc) noexcept
{
	T val{};
	if (desc.Ptr != 0 && desc.Size >= sizeof(T)) {
		std::memcpy(&val, reinterpret_cast<const void*>(static_cast<uintptr_t>(desc.Ptr)), sizeof(T));
	}
	return val;
}

void copy_bounded_str(
	std::array<char, kMaxCapturedNameLen + 1>& dst,
	const char* src,
	size_t max_src_bytes) noexcept
{
	dst.fill('\0');
	if (!src || max_src_bytes == 0) {
		return;
	}
	size_t len = 0;
	const size_t limit = std::min(max_src_bytes, kMaxCapturedNameLen);
	while (len < limit && src[len] != '\0') {
		dst[len] = src[len];
		++len;
	}
	dst[len] = '\0';
}

void read_descriptor_cstr_into(
	std::array<char, kMaxCapturedNameLen + 1>& dst,
	const EVENT_DATA_DESCRIPTOR& desc) noexcept
{
	if (desc.Ptr == 0 || desc.Size == 0) {
		dst.fill('\0');
		return;
	}
	const char* ptr = reinterpret_cast<const char*>(static_cast<uintptr_t>(desc.Ptr));
	copy_bounded_str(dst, ptr, desc.Size);
}

void extract_provider_name_into(
	std::array<char, kMaxCapturedNameLen + 1>& dst,
	const EVENT_DATA_DESCRIPTOR& desc) noexcept
{
	dst.fill('\0');
	if (desc.Ptr == 0 || desc.Size <= 2) {
		return;
	}
	const uint8_t* bytes = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(desc.Ptr));
	size_t pos = 2; // Skip UINT16 total size
	const size_t start = pos;
	while (pos < desc.Size && bytes[pos] != 0) {
		++pos;
	}
	copy_bounded_str(dst, reinterpret_cast<const char*>(bytes + start), pos - start);
}

void parse_event_metadata_into(
	CapturedEtwSlot& slot,
	const EVENT_DATA_DESCRIPTOR& desc) noexcept
{
	slot.event_name.fill('\0');
	slot.field_count = 0;
	if (desc.Ptr == 0 || desc.Size <= 3) {
		return;
	}
	const uint8_t* bytes = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(desc.Ptr));
	size_t pos = 2; // Skip UINT16 size
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
	copy_bounded_str(
		slot.event_name,
		reinterpret_cast<const char*>(bytes + name_start),
		pos - name_start);
	if (pos < desc.Size) {
		++pos; // Skip '\0'
	}

	while (pos < desc.Size && slot.field_count < kMaxCapturedFields) {
		const size_t field_start = pos;
		while (pos < desc.Size && bytes[pos] != 0) {
			++pos;
		}
		if (pos == field_start) {
			break;
		}
		copy_bounded_str(
			slot.field_names[slot.field_count],
			reinterpret_cast<const char*>(bytes + field_start),
			pos - field_start);
		++slot.field_count;
		if (pos < desc.Size) {
			++pos; // Skip '\0'
		}
		if (pos >= desc.Size) {
			break;
		}
		const uint8_t in_type = bytes[pos++];
		if ((in_type & 0x80u) != 0 && pos < desc.Size) {
			const uint8_t out_type = bytes[pos++];
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
	UCHAR level,
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

	CapturedEtwSlot slot{};
	slot.level = level;
	extract_provider_name_into(slot.provider_name, user_data[0]);
	parse_event_metadata_into(slot, user_data[1]);
	const std::string_view parsed_name(slot.event_name.data());

	if (parsed_name == "Diagnostic" && user_data_count >= 13) {
		slot.sequence = read_descriptor_scalar<uint64_t>(user_data[2]);
		slot.error_code = read_descriptor_scalar<uint32_t>(user_data[3]);
		read_descriptor_cstr_into(slot.error_code_name, user_data[4]);
		slot.diagnostic_id = read_descriptor_scalar<uint16_t>(user_data[5]);
		read_descriptor_cstr_into(slot.diagnostic_id_name, user_data[6]);
		slot.has_byte_offset = read_descriptor_scalar<uint8_t>(user_data[7]) != 0;
		slot.byte_offset = read_descriptor_scalar<uint64_t>(user_data[8]);
		slot.has_object_num = read_descriptor_scalar<uint8_t>(user_data[9]) != 0;
		slot.object_num = read_descriptor_scalar<uint32_t>(user_data[10]);
		slot.generation_num = read_descriptor_scalar<uint16_t>(user_data[11]);
		slot.thread_id = read_descriptor_scalar<uint32_t>(user_data[12]);
	} else if (parsed_name == "SpanStart" && user_data_count >= 6) {
		read_descriptor_cstr_into(slot.phase, user_data[2]);
		slot.document_id = read_descriptor_scalar<uint64_t>(user_data[3]);
		slot.page_index = read_descriptor_scalar<uint32_t>(user_data[4]);
		slot.thread_id = read_descriptor_scalar<uint32_t>(user_data[5]);
	} else if (parsed_name == "SpanEnd" && user_data_count >= 7) {
		read_descriptor_cstr_into(slot.phase, user_data[2]);
		slot.duration_us = read_descriptor_scalar<uint64_t>(user_data[3]);
		slot.document_id = read_descriptor_scalar<uint64_t>(user_data[4]);
		slot.page_index = read_descriptor_scalar<uint32_t>(user_data[5]);
		slot.thread_id = read_descriptor_scalar<uint32_t>(user_data[6]);
	}

	ScopedSrwLockExclusive lock(g_etw_capture_lock);
	if (g_etw_capture_count < kMaxCapturedEtwEvents) {
		g_etw_capture_slots[g_etw_capture_count++] = slot;
	}
}

} // namespace

} // namespace recto::pdf

namespace recto::pdf::detail {
std::atomic<uint64_t> g_etw_write_failures{0};
std::atomic<ULONG> g_etw_injected_write_status{ERROR_SUCCESS};
} // namespace recto::pdf::detail

extern "C" ULONG WINAPI RectoTlgEventWriteTransfer(
	REGHANDLE RegHandle,
	PCEVENT_DESCRIPTOR EventDescriptor,
	LPCGUID ActivityId,
	LPCGUID RelatedActivityId,
	ULONG UserDataCount,
	PEVENT_DATA_DESCRIPTOR UserData) noexcept
{
	recto::pdf::process_tlg_write_transfer(EventDescriptor ? EventDescriptor->Level : 0, UserDataCount, UserData);
	// What Windows answers is counted too: an event counted as emitted but refused by EventWriteTransfer is a failure
	// the tests can see (etw_write_failures).
	const ULONG injected = recto::pdf::detail::g_etw_injected_write_status.load(std::memory_order_acquire);
	const ULONG status = injected != ERROR_SUCCESS ? injected : ::EventWriteTransfer(
		RegHandle,
		EventDescriptor,
		ActivityId,
		RelatedActivityId,
		UserDataCount,
		UserData);
	if (status != ERROR_SUCCESS) {
		recto::pdf::detail::g_etw_write_failures.fetch_add(1, std::memory_order_relaxed);
	}
	return status;
}

namespace recto::pdf {

std::string PdfError::format() const noexcept
{
	try {
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
	} catch (...) {
		return {};
	}
}

std::string DiagnosticEntry::format() const noexcept
{
	try {
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
	} catch (...) {
		return {};
	}
}

DiagnosticId classify_diagnostic_id(ErrorCode code, std::string_view hint) noexcept
{
	// The hints name damaged-file, blocked-action and store categories. A resource limit or an invalid argument is
	// neither, so its own diagnostic stands whatever the message says.
	if (code == ErrorCode::ResourceLimit || code == ErrorCode::InvalidArgument) {
		return default_diagnostic_id(code);
	}
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

EtwPhaseId classify_etw_phase(std::string_view phase_name) noexcept
{
	if (ascii_iequals(phase_name, "OpenDocument") || ascii_iequals(phase_name, "Open")) {
		return EtwPhaseId::OpenDocument;
	}
	if (ascii_iequals(phase_name, "ParseXRef")) {
		return EtwPhaseId::ParseXRef;
	}
	if (ascii_iequals(phase_name, "RecoverXRef")) {
		return EtwPhaseId::RecoverXRef;
	}
	if (ascii_iequals(phase_name, "DecodeFilter")) {
		return EtwPhaseId::DecodeFilter;
	}
	if (ascii_iequals(phase_name, "DecryptObject")) {
		return EtwPhaseId::DecryptObject;
	}
	if (ascii_iequals(phase_name, "InterpretPage")) {
		return EtwPhaseId::InterpretPage;
	}
	if (ascii_iequals(phase_name, "BuildDisplayList")) {
		return EtwPhaseId::BuildDisplayList;
	}
	if (ascii_iequals(phase_name, "LoadFont")) {
		return EtwPhaseId::LoadFont;
	}
	if (ascii_iequals(phase_name, "RenderTile") || ascii_iequals(phase_name, "Render")) {
		return EtwPhaseId::RenderTile;
	}
	if (ascii_iequals(phase_name, "ExtractPageText")) {
		return EtwPhaseId::ExtractPageText;
	}
	if (ascii_iequals(phase_name, "RunOcr") || ascii_iequals(phase_name, "Ocr")) {
		return EtwPhaseId::RunOcr;
	}
	if (ascii_iequals(phase_name, "ConvertDocument") || ascii_iequals(phase_name, "Convert")) {
		return EtwPhaseId::ConvertDocument;
	}
	if (ascii_iequals(phase_name, "OrganizeDocument") || ascii_iequals(phase_name, "Organize")) {
		return EtwPhaseId::OrganizeDocument;
	}
	if (ascii_iequals(phase_name, "SaveDocument") || ascii_iequals(phase_name, "Save")) {
		return EtwPhaseId::SaveDocument;
	}
	if (ascii_iequals(phase_name, "SignDocument") || ascii_iequals(phase_name, "Sign")) {
		return EtwPhaseId::SignDocument;
	}
	if (ascii_iequals(phase_name, "VerifySignature") || ascii_iequals(phase_name, "Verify")) {
		return EtwPhaseId::VerifySignature;
	}
	if (ascii_iequals(phase_name, "RedactDocument") || ascii_iequals(phase_name, "Redact")) {
		return EtwPhaseId::RedactDocument;
	}
	if (ascii_iequals(phase_name, "ValidateCompliance") || ascii_iequals(phase_name, "Compliance")) {
		return EtwPhaseId::ValidateCompliance;
	}
	return EtwPhaseId::UnspecifiedPhase;
}

bool ensure_etw_provider_registered() noexcept
{
	if (g_etw_registered.load(std::memory_order_acquire)) {
		return true;
	}
	ScopedSrwLockExclusive lock(g_etw_lock);
	if (!g_etw_registered.load(std::memory_order_relaxed)) {
		static EtwProviderUnregistrar unregistrar{};
		const HRESULT hr = TraceLoggingRegister(g_recto_etw_provider);
		if (SUCCEEDED(hr)) {
			// Registered only: TraceLoggingWrite builds and writes an event only while a session has enabled the
			// provider, so with nobody listening, diagnostics cost nothing beyond the ring (D16).
			g_etw_registered.store(true, std::memory_order_release);
		}
	}
	return g_etw_registered.load(std::memory_order_relaxed);
}

bool etw_provider_enabled() noexcept
{
	return ensure_etw_provider_registered() && TraceLoggingProviderEnabled(g_recto_etw_provider, 0, 0);
}

void force_enable_etw_provider_for_testing() noexcept
{
	if (!ensure_etw_provider_registered() || g_recto_etw_provider->LevelPlus1 != 0) {
		return;
	}
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

uint64_t etw_write_failures() noexcept
{
	return detail::g_etw_write_failures.load(std::memory_order_relaxed);
}

void set_etw_write_status_for_testing(unsigned long status) noexcept
{
	detail::g_etw_injected_write_status.store(status, std::memory_order_release);
}

void set_etw_capture_for_testing(bool enabled) noexcept
{
	ensure_etw_provider_registered();
	ScopedSrwLockExclusive lock(g_etw_capture_lock);
	g_etw_capture_enabled.store(enabled, std::memory_order_release);
}

void clear_captured_etw_events_for_testing() noexcept
{
	ScopedSrwLockExclusive lock(g_etw_capture_lock);
	g_etw_capture_slots.fill(CapturedEtwSlot{});
	g_etw_capture_count = 0;
}

std::vector<CapturedEtwEvent> captured_etw_events_for_testing() noexcept
{
	std::array<CapturedEtwSlot, kMaxCapturedEtwEvents> snapshot{};
	size_t count = 0;
	{
		ScopedSrwLockShared lock(g_etw_capture_lock);
		count = g_etw_capture_count;
		for (size_t i = 0; i < count; ++i) {
			snapshot[i] = g_etw_capture_slots[i];
		}
	}

	try {
		std::vector<CapturedEtwEvent> out;
		out.reserve(count);
		for (size_t i = 0; i < count; ++i) {
			const CapturedEtwSlot& s = snapshot[i];
			CapturedEtwEvent ev{};
			ev.provider_name = s.provider_name.data();
			ev.event_name = s.event_name.data();
			ev.field_names.reserve(s.field_count);
			for (uint8_t f = 0; f < s.field_count; ++f) {
				ev.field_names.emplace_back(s.field_names[f].data());
			}
			ev.level = s.level;
			ev.sequence = s.sequence;
			ev.error_code = s.error_code;
			ev.error_code_name = s.error_code_name.data();
			ev.diagnostic_id = s.diagnostic_id;
			ev.diagnostic_id_name = s.diagnostic_id_name.data();
			ev.has_byte_offset = s.has_byte_offset;
			ev.byte_offset = s.byte_offset;
			ev.has_object_num = s.has_object_num;
			ev.object_num = s.object_num;
			ev.generation_num = s.generation_num;
			ev.phase = s.phase.data();
			ev.duration_us = s.duration_us;
			ev.document_id = s.document_id;
			ev.page_index = s.page_index;
			ev.thread_id = s.thread_id;
			out.push_back(std::move(ev));
		}
		return out;
	} catch (...) {
		return {};
	}
}

void record_diagnostic(
	ErrorCode code,
	DiagnosticId diagnostic_id,
	std::optional<uint64_t> byte_offset,
	std::optional<uint32_t> object_num,
	uint16_t generation_num) noexcept
{
	ensure_etw_provider_registered();

	const uint32_t tid = static_cast<uint32_t>(GetCurrentThreadId());

	uint64_t assigned_seq = 0;
	{
		ScopedSrwLockExclusive lock(g_ring_lock);
		DiagnosticSlot& slot = g_ring_slots[g_ring_write_index];
		assigned_seq = g_ring_next_sequence++;
		slot.sequence = assigned_seq;
		// Wall-clock UTC for people reading the log; order is the sequence number (system time can be set back).
		slot.timestamp_utc_ms = current_utc_timestamp_ms();
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
		TraceLoggingLevel(WINEVENT_LEVEL_ERROR), // every diagnostic is a PdfError returned to a caller
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

std::vector<DiagnosticEntry> diagnostics_tail(size_t max_entries) noexcept
{
	if (max_entries == 0) {
		return {};
	}

	std::array<DiagnosticSlot, kDiagnosticRingCapacity> snapshot{};
	size_t count = 0;
	{
		ScopedSrwLockShared lock(g_ring_lock);
		count = std::min(max_entries, g_ring_count);
		if (count > 0) {
			const size_t start_index =
				(g_ring_write_index + kDiagnosticRingCapacity - count) % kDiagnosticRingCapacity;
			for (size_t i = 0; i < count; ++i) {
				snapshot[i] = g_ring_slots[(start_index + i) % kDiagnosticRingCapacity];
			}
		}
	}

	try {
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
	} catch (...) {
		return {};
	}
}

void clear_diagnostics() noexcept
{
	ScopedSrwLockExclusive lock(g_ring_lock);
	g_ring_slots.fill(DiagnosticSlot{});
	g_ring_write_index = 0;
	g_ring_count = 0;
	g_ring_next_sequence = 1;
}

uint64_t diagnostics_total_recorded() noexcept
{
	ScopedSrwLockShared lock(g_ring_lock);
	return g_ring_next_sequence - 1;
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
	EtwPhaseId phase_id,
	uint64_t document_id,
	uint32_t page_index) noexcept
	: phase_id_(phase_id)
	, document_id_(document_id)
	, page_index_(page_index)
	, thread_id_(static_cast<uint32_t>(GetCurrentThreadId()))
	, start_qpc_(qpc_now())
{
	ensure_etw_provider_registered();
	TraceLoggingWrite(
		g_recto_etw_provider,
		"SpanStart",
		TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE), // tracing detail
		TraceLoggingString(etw_phase_cstr(phase_id_), "Phase"),
		TraceLoggingUInt64(document_id_, "DocumentId"),
		TraceLoggingUInt32(page_index_, "PageIndex"),
		TraceLoggingUInt32(thread_id_, "ThreadId"));
}

EtwSpan::EtwSpan(
	std::string_view phase_name,
	uint64_t document_id,
	uint32_t page_index) noexcept
	: EtwSpan(classify_etw_phase(phase_name), document_id, page_index)
{
}

EtwSpan::~EtwSpan() noexcept
{
	const uint64_t duration_us = elapsed_us();
	TraceLoggingWrite(
		g_recto_etw_provider,
		"SpanEnd",
		TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE),
		TraceLoggingString(etw_phase_cstr(phase_id_), "Phase"),
		TraceLoggingUInt64(duration_us, "DurationUs"),
		TraceLoggingUInt64(document_id_, "DocumentId"),
		TraceLoggingUInt32(page_index_, "PageIndex"),
		TraceLoggingUInt32(thread_id_, "ThreadId"));
}

uint64_t EtwSpan::elapsed_us() const noexcept
{
	return qpc_delta_to_us(start_qpc_, qpc_now());
}

} // namespace recto::pdf
