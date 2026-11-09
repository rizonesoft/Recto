// Temp-file-then-replace in the destination folder (D01 T02 section 8).
#include "atomic_write.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>
#include <string>
#include <system_error>
#include <vector>

namespace recto::core {

namespace {

using pdf::ErrorCode;
using pdf::make_pdf_error;
using pdf::PdfResult;

thread_local detail::AtomicWriteStage g_fail_stage = detail::AtomicWriteStage::None;
thread_local uint32_t g_fail_error = 0u;
thread_local detail::AtomicWriteHook g_hook = nullptr;
thread_local void* g_hook_context = nullptr;

void reached(detail::AtomicWriteStage stage, const std::wstring& temporary) noexcept
{
	if (g_hook != nullptr) {
		try {
			g_hook(stage, std::filesystem::path(temporary), g_hook_context);
		} catch (const std::bad_alloc&) {
			// A path the hook could not be given: the hook is skipped, the write goes on.
		}
	}
}

// 4 MiB: a cancel waits at most one chunk's read and compare, about a millisecond from the cache.
constexpr DWORD kChunkBytes = 4u << 20u;
constexpr int kReplaceAttempts = 3;
constexpr DWORD kReplaceRetryMs = 50u;

// The error injected at `stage`, consumed, or 0.
[[nodiscard]] DWORD injected(detail::AtomicWriteStage stage) noexcept
{
	if (g_fail_stage != stage) {
		return 0u;
	}
	g_fail_stage = detail::AtomicWriteStage::None;
	return g_fail_error;
}

class Handle {
public:
	explicit Handle(HANDLE handle) noexcept
		: handle_(handle)
	{
	}
	~Handle() noexcept
	{
		reset();
	}
	Handle(const Handle&) = delete;
	Handle& operator=(const Handle&) = delete;
	void reset() noexcept
	{
		if (handle_ != INVALID_HANDLE_VALUE) {
			::CloseHandle(handle_);
			handle_ = INVALID_HANDLE_VALUE;
		}
	}
	[[nodiscard]] HANDLE get() const noexcept
	{
		return handle_;
	}
	[[nodiscard]] bool valid() const noexcept
	{
		return handle_ != INVALID_HANDLE_VALUE;
	}

private:
	HANDLE handle_;
};

[[nodiscard]] pdf::PdfError write_failure(DWORD error, const char* what) noexcept
{
	return make_pdf_error(error == ERROR_NOT_ENOUGH_MEMORY || error == ERROR_OUTOFMEMORY ? ErrorCode::OutOfMemory : ErrorCode::IoError, what);
}

// Whether the file at `path` holds exactly `bytes`; `ERROR_CANCELLED` when `cancel` is seen between chunks.
[[nodiscard]] bool file_holds(const std::wstring& path, std::span<const uint8_t> bytes, DWORD& error, const pdf::CancelToken& cancel = {}) noexcept
{
	Handle file(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
	if (!file.valid()) {
		error = ::GetLastError();
		return false;
	}
	LARGE_INTEGER size{};
	if (::GetFileSizeEx(file.get(), &size) == FALSE) {
		error = ::GetLastError();
		return false;
	}
	if (static_cast<uint64_t>(size.QuadPart) != bytes.size()) {
		error = ERROR_FILE_CORRUPT;
		return false;
	}
	std::vector<uint8_t> chunk;
	try {
		chunk.resize(std::min<size_t>(bytes.size(), kChunkBytes));
	} catch (const std::bad_alloc&) {
		error = ERROR_OUTOFMEMORY;
		return false;
	}
	size_t at = 0u;
	while (at < bytes.size()) {
		if (at != 0u) {
			reached(detail::AtomicWriteStage::ReadBackChunk, path);
		}
		if (cancel.is_cancelled()) {
			error = ERROR_CANCELLED;
			return false;
		}
		const DWORD want = static_cast<DWORD>(std::min<size_t>(bytes.size() - at, kChunkBytes));
		DWORD got = 0u;
		if (::ReadFile(file.get(), chunk.data(), want, &got, nullptr) == FALSE || got != want) {
			error = ::GetLastError();
			return false;
		}
		if (std::memcmp(chunk.data(), bytes.data() + at, want) != 0) {
			error = ERROR_FILE_CORRUPT;
			return false;
		}
		at += want;
	}
	return true;
}

// Owner and SYSTEM only, inheriting nothing: what a temporary copy of an existing file carries until `ReplaceFileW` gives
// it the destination's own ACL, so a file restricted inside a shared folder is never readable through its copy.
class PrivateSecurity {
public:
	PrivateSecurity() noexcept
	{
		if (::ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;OW)(A;;FA;;;SY)", SDDL_REVISION_1, &descriptor_, nullptr) == FALSE) {
			descriptor_ = nullptr;
		}
	}
	~PrivateSecurity() noexcept
	{
		if (descriptor_ != nullptr) {
			::LocalFree(descriptor_);
		}
	}
	PrivateSecurity(const PrivateSecurity&) = delete;
	PrivateSecurity& operator=(const PrivateSecurity&) = delete;
	[[nodiscard]] PSECURITY_DESCRIPTOR get() const noexcept
	{
		return descriptor_;
	}

private:
	PSECURITY_DESCRIPTOR descriptor_{nullptr};
};

// A new file beside `destination`, in the same folder so the replace stays on one volume. A copy of an existing file is
// private until it replaces it; a new one takes the folder's inherited ACL, which is what the saved file will have. It
// allocates, so a `std::bad_alloc` reaches the caller's handler.
[[nodiscard]] HANDLE create_temporary(const std::filesystem::path& destination, std::wstring& path)
{
	static std::atomic<uint32_t> counter{0u};
	std::error_code ignored;
	const std::filesystem::path folder = destination.has_parent_path() ? destination.parent_path() : std::filesystem::current_path(ignored);
	const bool replacing = ::GetFileAttributesW(destination.c_str()) != INVALID_FILE_ATTRIBUTES;
	const PrivateSecurity security;
	if (replacing && security.get() == nullptr) {
		::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return INVALID_HANDLE_VALUE; // never a readable copy of a file that may be restricted
	}
	SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), security.get(), FALSE};
	for (int attempt = 0; attempt < 64; ++attempt) {
		wchar_t name[64];
		::swprintf_s(name, L".~recto-%08lx-%08x.tmp", ::GetCurrentProcessId(), counter.fetch_add(1u));
		path = (folder / name).wstring();
		const HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0u, replacing ? &attributes : nullptr, CREATE_NEW,
			FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_HIDDEN, nullptr);
		if (handle != INVALID_HANDLE_VALUE || ::GetLastError() != ERROR_FILE_EXISTS) {
			return handle;
		}
	}
	return INVALID_HANDLE_VALUE;
}

} // namespace

namespace detail {

ScopedAtomicWriteFailureForTesting::ScopedAtomicWriteFailureForTesting(AtomicWriteStage stage, uint32_t win32_error) noexcept
	: previous_stage_(g_fail_stage)
	, previous_error_(g_fail_error)
{
	g_fail_stage = stage;
	g_fail_error = win32_error;
}

ScopedAtomicWriteFailureForTesting::~ScopedAtomicWriteFailureForTesting() noexcept
{
	g_fail_stage = previous_stage_;
	g_fail_error = previous_error_;
}

ScopedAtomicWriteHookForTesting::ScopedAtomicWriteHookForTesting(AtomicWriteHook hook, void* context) noexcept
	: previous_hook_(g_hook)
	, previous_context_(g_hook_context)
{
	g_hook = hook;
	g_hook_context = context;
}

ScopedAtomicWriteHookForTesting::~ScopedAtomicWriteHookForTesting() noexcept
{
	g_hook = previous_hook_;
	g_hook_context = previous_context_;
}

} // namespace detail

PdfResult<void> atomic_write_file(
	const std::filesystem::path& destination, std::span<const uint8_t> bytes, const pdf::CancelToken& cancel) noexcept
{
	try {
		const std::wstring target = destination.wstring();
		std::wstring temporary;
		Handle file(create_temporary(destination, temporary));
		if (!file.valid()) {
			return write_failure(::GetLastError(), "Could not create a temporary file beside the destination");
		}
		// Until the replace, the temporary file goes on any way out.
		struct Remove {
			const std::wstring& path;
			bool active{true};
			~Remove() noexcept
			{
				if (active) {
					reached(detail::AtomicWriteStage::Cleanup, path);
					::DeleteFileW(path.c_str()); // the system's own work: a large file takes milliseconds to let go
				}
			}
		} remove{temporary};
		// A temporary file carries the attributes it was made with; the published one takes the normal ones.
		size_t at = 0u;
		while (at < bytes.size()) {
			if (cancel.is_cancelled()) {
				file.reset();
				return pdf::detail::cancelled_error();
			}
			const DWORD want = static_cast<DWORD>(std::min<size_t>(bytes.size() - at, kChunkBytes));
			DWORD wrote = 0u;
			if (const DWORD error = injected(detail::AtomicWriteStage::Write); error != 0u) {
				file.reset();
				return write_failure(error, "Could not write the temporary file; the destination is unchanged");
			}
			if (::WriteFile(file.get(), bytes.data() + at, want, &wrote, nullptr) == FALSE || wrote != want) {
				const DWORD error = ::GetLastError();
				file.reset();
				return write_failure(error, "Could not write the temporary file; the destination is unchanged");
			}
			at += want;
		}
		if (const DWORD error = injected(detail::AtomicWriteStage::Flush); error != 0u) {
			file.reset();
			return write_failure(error, "Could not flush the temporary file; the destination is unchanged");
		}
		if (::FlushFileBuffers(file.get()) == FALSE) {
			const DWORD error = ::GetLastError();
			file.reset();
			return write_failure(error, "Could not flush the temporary file; the destination is unchanged");
		}
		file.reset();
		reached(detail::AtomicWriteStage::ReadBack, temporary);
		DWORD read_error = injected(detail::AtomicWriteStage::ReadBack);
		if (read_error != 0u || !file_holds(temporary, bytes, read_error, cancel)) {
			if (read_error == ERROR_CANCELLED) {
				return pdf::detail::cancelled_error();
			}
			return write_failure(read_error, "The temporary file did not read back as written; the destination is unchanged");
		}
		::SetFileAttributesW(temporary.c_str(), FILE_ATTRIBUTE_NORMAL);
		// Whether a file was there decides what a failed replace leaves.
		const bool had_destination = ::GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES;
		reached(detail::AtomicWriteStage::Replace, temporary);
		if (cancel.is_cancelled()) {
			return pdf::detail::cancelled_error();
		}
		// The commit point.
		DWORD replace_error = injected(detail::AtomicWriteStage::Replace);
		for (int attempt = 0; replace_error == 0u && attempt < kReplaceAttempts; ++attempt) {
			if (attempt != 0) {
				// A cancel while a scanner held the file is still before the commit point.
				reached(detail::AtomicWriteStage::Retry, temporary);
				if (cancel.is_cancelled()) {
					return pdf::detail::cancelled_error();
				}
			}
			const DWORD existing = ::GetFileAttributesW(target.c_str());
			const BOOL done = existing != INVALID_FILE_ATTRIBUTES
				? ::ReplaceFileW(target.c_str(), temporary.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)
				: ::MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
			if (done != FALSE) {
				remove.active = false;
				break;
			}
			replace_error = ::GetLastError();
			if (replace_error == ERROR_SHARING_VIOLATION || replace_error == ERROR_LOCK_VIOLATION) {
				if (attempt + 1 < kReplaceAttempts) {
					::Sleep(kReplaceRetryMs);
					replace_error = 0u;
				}
			} else {
				break;
			}
		}
		if (replace_error != 0u) {
			if (had_destination && ::GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES && ::GetLastError() != ERROR_ACCESS_DENIED) {
				// The destination is gone and this is the only copy: it stays, named so it can be found.
				remove.active = false;
				const std::string message = "The file could not be put in place, and the destination is gone; your data is in " + std::filesystem::path(temporary).string();
				return write_failure(replace_error, message.c_str());
			}
			return write_failure(replace_error, "The file could not be put in place; the destination is unchanged");
		}
		reached(detail::AtomicWriteStage::Verify, temporary);
		DWORD verify_error = injected(detail::AtomicWriteStage::Verify);
		if (verify_error != 0u || !file_holds(target, bytes, verify_error)) {
			const std::string message = "The saved file did not read back as written: " + destination.string();
			return write_failure(verify_error, message.c_str());
		}
		return {};
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory writing a file");
	} catch (const std::system_error&) {
		return make_pdf_error(ErrorCode::IoError, "A file path could not be used");
	}
}

} // namespace recto::core
