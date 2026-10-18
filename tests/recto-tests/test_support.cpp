#include "test_support.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace recto::test {

namespace {

std::atomic<uint64_t> g_temp_dir_counter{1};
std::atomic<WriteFailStage> g_fail_next_write{WriteFailStage::None};

std::wstring utf8_to_wide(std::string_view utf8)
{
	if (utf8.empty()) {
		return {};
	}
	const int needed = MultiByteToWideChar(
		CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
	if (needed <= 0) {
		throw std::runtime_error("Invalid UTF-8 string in test_support");
	}
	std::wstring result(static_cast<size_t>(needed), L'\0');
	MultiByteToWideChar(
		CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), result.data(), needed);
	return result;
}

std::filesystem::path executable_path()
{
	std::wstring buffer(32768, L'\0');
	const DWORD copied = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
	if (copied == 0 || copied >= buffer.size()) {
		throw std::runtime_error("GetModuleFileNameW failed in test_support");
	}
	buffer.resize(copied);
	return std::filesystem::path(buffer);
}

struct UniqueHandle {
	HANDLE handle{INVALID_HANDLE_VALUE};

	explicit UniqueHandle(HANDLE h) noexcept : handle(h) {}
	~UniqueHandle() noexcept
	{
		close();
	}

	UniqueHandle(const UniqueHandle&) = delete;
	UniqueHandle& operator=(const UniqueHandle&) = delete;

	void close() noexcept
	{
		if (handle != INVALID_HANDLE_VALUE) {
			CloseHandle(handle);
			handle = INVALID_HANDLE_VALUE;
		}
	}
};

struct TempFileGuard {
	std::filesystem::path path;
	bool armed{false};
	bool committed{false};

	TempFileGuard() noexcept = default;
	~TempFileGuard() noexcept
	{
		if (armed && !committed && !path.empty()) {
			DeleteFileW(path.c_str());
		}
	}

	TempFileGuard(const TempFileGuard&) = delete;
	TempFileGuard& operator=(const TempFileGuard&) = delete;
};

void clear_readonly_recursive(const std::filesystem::path& root) noexcept
{
	try {
		std::error_code ec;
		const auto long_root = to_long_path(root);
		const DWORD root_attrs = GetFileAttributesW(long_root.c_str());
		if (root_attrs != INVALID_FILE_ATTRIBUTES && (root_attrs & FILE_ATTRIBUTE_READONLY) != 0) {
			SetFileAttributesW(long_root.c_str(), root_attrs & ~FILE_ATTRIBUTE_READONLY);
		}
		for (std::filesystem::recursive_directory_iterator it(
				 long_root, std::filesystem::directory_options::skip_permission_denied, ec),
			 end;
			 it != end;
			 it.increment(ec)) {
			if (ec) {
				ec.clear();
				continue;
			}
			const auto& entry_path = it->path();
			const DWORD attrs = GetFileAttributesW(entry_path.c_str());
			if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) != 0) {
				SetFileAttributesW(entry_path.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
			}
		}
	} catch (...) {
	}
}

} // namespace

std::filesystem::path to_long_path(const std::filesystem::path& path)
{
	if (path.empty()) {
		return path;
	}
	std::error_code ec;
	std::filesystem::path abs = path.is_absolute() ? path : std::filesystem::absolute(path, ec);
	if (ec) {
		abs = path;
	}
	abs = abs.lexically_normal();
	std::wstring native = abs.native();
	if (native.rfind(L"\\\\?\\", 0) == 0) {
		return abs;
	}
	if (native.rfind(L"\\\\", 0) == 0) {
		return std::filesystem::path(L"\\\\?\\UNC\\" + native.substr(2));
	}
	return std::filesystem::path(L"\\\\?\\" + native);
}

std::filesystem::path repo_root()
{
	std::filesystem::path current = executable_path().parent_path();
	while (!current.empty()) {
		std::error_code ec;
		if (std::filesystem::exists(current / L"Recto.sln", ec) &&
			std::filesystem::exists(current / L"tests" / L"recto-tests", ec)) {
			return current.lexically_normal();
		}
		const auto parent = current.parent_path();
		if (parent == current) {
			break;
		}
		current = parent;
	}
	throw std::runtime_error("Could not locate Recto repository root from executable path");
}

std::filesystem::path corpus_path(std::string_view relative)
{
	std::filesystem::path base;
	const DWORD env_len = GetEnvironmentVariableW(L"RECTO_CORPUS_DIR", nullptr, 0);
	if (env_len > 1) {
		std::wstring env_val(env_len - 1, L'\0');
		const DWORD copied = GetEnvironmentVariableW(L"RECTO_CORPUS_DIR", env_val.data(), env_len);
		if (copied > 0 && copied < env_len && !env_val.empty()) {
			base = std::filesystem::path(env_val);
		}
	}
	if (base.empty()) {
		base = repo_root() / L"tests" / L"corpus";
	}
	if (relative.empty()) {
		return base.lexically_normal();
	}
	const std::filesystem::path rel_path(utf8_to_wide(relative));
	return (base / rel_path).lexically_normal();
}

std::vector<uint8_t> read_file_bytes(const std::filesystem::path& path)
{
	const auto long_path = to_long_path(path);
	UniqueHandle file(CreateFileW(
		long_path.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr));
	if (file.handle == INVALID_HANDLE_VALUE) {
		throw std::runtime_error("read_file_bytes: CreateFileW failed");
	}
	LARGE_INTEGER file_size{};
	if (!GetFileSizeEx(file.handle, &file_size) || file_size.QuadPart < 0) {
		throw std::runtime_error("read_file_bytes: GetFileSizeEx failed");
	}
	std::vector<uint8_t> buffer(static_cast<size_t>(file_size.QuadPart));
	size_t offset = 0;
	while (offset < buffer.size()) {
		const DWORD chunk = static_cast<DWORD>(
			std::min<size_t>(buffer.size() - offset, 1u << 20));
		DWORD bytes_read = 0;
		if (!ReadFile(file.handle, buffer.data() + offset, chunk, &bytes_read, nullptr)) {
			throw std::runtime_error("read_file_bytes: ReadFile failed");
		}
		if (bytes_read == 0) {
			break;
		}
		offset += bytes_read;
	}
	buffer.resize(offset);
	return buffer;
}

void fail_next_write_for_testing(WriteFailStage stage) noexcept
{
	g_fail_next_write.store(stage, std::memory_order_relaxed);
}

void write_file_bytes(const std::filesystem::path& path, std::span<const uint8_t> data)
{
	const auto long_path = to_long_path(path);
	const auto parent_dir = long_path.parent_path();
	if (!parent_dir.empty()) {
		std::error_code ec;
		std::filesystem::create_directories(parent_dir, ec);
		if (ec) {
			throw std::runtime_error("write_file_bytes: failed to create parent directory: " + ec.message());
		}
	}

	const DWORD pid = GetCurrentProcessId();
	// Declare temp_guard before file so stack unwinding destroys file (closing
	// the handle) before ~TempFileGuard calls DeleteFileW.
	TempFileGuard temp_guard;
	UniqueHandle file(INVALID_HANDLE_VALUE);
	for (int attempt = 0; attempt < 64; ++attempt) {
		LARGE_INTEGER qpc{};
		QueryPerformanceCounter(&qpc);
		const uint64_t seq = g_temp_dir_counter.fetch_add(1, std::memory_order_relaxed);
		const std::wstring tmp_name = L".recto-tmp-" +
			std::to_wstring(pid) + L"-" +
			std::to_wstring(static_cast<uint64_t>(qpc.QuadPart)) + L"-" +
			std::to_wstring(seq) + L".tmp";
		temp_guard.path = parent_dir.empty() ? std::filesystem::path(tmp_name) : (parent_dir / tmp_name);
		HANDLE h = CreateFileW(
			temp_guard.path.c_str(),
			GENERIC_WRITE,
			0,
			nullptr,
			CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (h != INVALID_HANDLE_VALUE) {
			file.handle = h;
			temp_guard.armed = true;
			break;
		}
		const DWORD err = GetLastError();
		if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS) {
			throw std::runtime_error("write_file_bytes: CreateFileW failed for temp file");
		}
	}
	if (file.handle == INVALID_HANDLE_VALUE) {
		throw std::runtime_error("write_file_bytes: exhausted retries creating unique temp file");
	}
	const WriteFailStage fail_stage =
		g_fail_next_write.exchange(WriteFailStage::None, std::memory_order_relaxed);
	if (fail_stage == WriteFailStage::AfterAcquire) {
		throw std::runtime_error("write_file_bytes: injected failure at handle acquisition boundary");
	}

	size_t offset = 0;
	while (offset < data.size()) {
		const DWORD chunk = static_cast<DWORD>(
			std::min<size_t>(data.size() - offset, 1u << 20));
		DWORD bytes_written = 0;
		if (!WriteFile(file.handle, data.data() + offset, chunk, &bytes_written, nullptr)) {
			throw std::runtime_error("write_file_bytes: WriteFile failed");
		}
		offset += bytes_written;
	}
	if (fail_stage == WriteFailStage::AfterWrite) {
		throw std::runtime_error("write_file_bytes: injected failure before handle close");
	}
	if (!FlushFileBuffers(file.handle)) {
		throw std::runtime_error("write_file_bytes: FlushFileBuffers failed");
	}
	file.close();

	if (!MoveFileExW(
			temp_guard.path.c_str(),
			long_path.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		throw std::runtime_error("write_file_bytes: MoveFileExW atomic replace failed");
	}
	temp_guard.committed = true;
}

TempDir::TempDir()
{
	const DWORD needed = GetTempPathW(0, nullptr);
	if (needed == 0) {
		throw std::runtime_error("GetTempPathW sizing failed in TempDir");
	}
	std::wstring temp_buf(needed, L'\0');
	const DWORD len = GetTempPathW(needed, temp_buf.data());
	if (len == 0 || len >= needed) {
		throw std::runtime_error("GetTempPathW failed in TempDir");
	}
	temp_buf.resize(len);
	const std::filesystem::path base_temp = std::filesystem::path(temp_buf).lexically_normal();

	std::error_code ec;
	std::filesystem::create_directories(to_long_path(base_temp), ec);
	if (ec) {
		throw std::runtime_error("Failed to ensure base temp directory: " + ec.message());
	}

	const DWORD pid = GetCurrentProcessId();
	for (int attempt = 0; attempt < 64; ++attempt) {
		LARGE_INTEGER qpc{};
		QueryPerformanceCounter(&qpc);
		const uint64_t seq = g_temp_dir_counter.fetch_add(1, std::memory_order_relaxed);
		const std::wstring dir_name = L"recto-test-" + std::to_wstring(pid) + L"-" +
			std::to_wstring(static_cast<uint64_t>(qpc.QuadPart)) + L"-" + std::to_wstring(seq);
		const std::filesystem::path candidate = (base_temp / dir_name).lexically_normal();
		const auto candidate_long = to_long_path(candidate);
		if (CreateDirectoryW(candidate_long.c_str(), nullptr) != 0) {
			path_ = candidate;
			return;
		}
		const DWORD err = GetLastError();
		if (err != ERROR_ALREADY_EXISTS) {
			throw std::runtime_error("CreateDirectoryW failed in TempDir");
		}
	}
	throw std::runtime_error("Failed to create exclusive TempDir after retries");
}

TempDir::~TempDir() noexcept
{
	cleanup();
}

TempDir::TempDir(TempDir&& other) noexcept : path_(std::move(other.path_))
{
	other.path_.clear();
}

TempDir& TempDir::operator=(TempDir&& other) noexcept
{
	if (this != &other) {
		cleanup();
		path_ = std::move(other.path_);
		other.path_.clear();
	}
	return *this;
}

std::filesystem::path TempDir::file_path(std::string_view relative) const
{
	if (relative.empty()) {
		return path_;
	}
	return (path_ / std::filesystem::path(utf8_to_wide(relative))).lexically_normal();
}

void TempDir::cleanup() noexcept
{
	if (path_.empty()) {
		return;
	}
	try {
		clear_readonly_recursive(path_);
		std::error_code ec;
		std::filesystem::remove_all(to_long_path(path_), ec);
	} catch (...) {
	}
	path_.clear();
}

} // namespace recto::test
