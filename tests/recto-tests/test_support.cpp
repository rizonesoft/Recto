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

void clear_readonly_recursive(const std::filesystem::path& root) noexcept
{
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
	const HANDLE file = CreateFileW(
		long_path.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		throw std::runtime_error("read_file_bytes: CreateFileW failed");
	}
	LARGE_INTEGER file_size{};
	if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart < 0) {
		CloseHandle(file);
		throw std::runtime_error("read_file_bytes: GetFileSizeEx failed");
	}
	std::vector<uint8_t> buffer(static_cast<size_t>(file_size.QuadPart));
	size_t offset = 0;
	while (offset < buffer.size()) {
		const DWORD chunk = static_cast<DWORD>(
			std::min<size_t>(buffer.size() - offset, 1u << 20));
		DWORD bytes_read = 0;
		if (!ReadFile(file, buffer.data() + offset, chunk, &bytes_read, nullptr)) {
			CloseHandle(file);
			throw std::runtime_error("read_file_bytes: ReadFile failed");
		}
		if (bytes_read == 0) {
			break;
		}
		offset += bytes_read;
	}
	CloseHandle(file);
	buffer.resize(offset);
	return buffer;
}

void write_file_bytes(const std::filesystem::path& path, std::span<const uint8_t> data)
{
	const auto long_path = to_long_path(path);
	std::error_code ec;
	if (long_path.has_parent_path()) {
		std::filesystem::create_directories(long_path.parent_path(), ec);
	}
	const HANDLE file = CreateFileW(
		long_path.c_str(),
		GENERIC_WRITE,
		0,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		throw std::runtime_error("write_file_bytes: CreateFileW failed");
	}
	size_t offset = 0;
	while (offset < data.size()) {
		const DWORD chunk = static_cast<DWORD>(
			std::min<size_t>(data.size() - offset, 1u << 20));
		DWORD bytes_written = 0;
		if (!WriteFile(file, data.data() + offset, chunk, &bytes_written, nullptr)) {
			CloseHandle(file);
			throw std::runtime_error("write_file_bytes: WriteFile failed");
		}
		offset += bytes_written;
	}
	CloseHandle(file);
}

TempDir::TempDir()
{
	std::wstring temp_buf(MAX_PATH + 1, L'\0');
	const DWORD len = GetTempPathW(static_cast<DWORD>(temp_buf.size()), temp_buf.data());
	if (len == 0 || len >= temp_buf.size()) {
		throw std::runtime_error("GetTempPathW failed in TempDir");
	}
	temp_buf.resize(len);
	const std::filesystem::path base_temp = std::filesystem::path(temp_buf).lexically_normal();

	const DWORD pid = GetCurrentProcessId();
	LARGE_INTEGER qpc{};
	QueryPerformanceCounter(&qpc);
	const uint64_t seq = g_temp_dir_counter.fetch_add(1, std::memory_order_relaxed);

	const std::wstring dir_name = L"recto-test-" + std::to_wstring(pid) + L"-" +
		std::to_wstring(static_cast<uint64_t>(qpc.QuadPart)) + L"-" + std::to_wstring(seq);
	path_ = (base_temp / dir_name).lexically_normal();

	std::error_code ec;
	std::filesystem::create_directories(to_long_path(path_), ec);
	if (ec) {
		throw std::runtime_error("Failed to create TempDir: " + ec.message());
	}
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
	clear_readonly_recursive(path_);
	std::error_code ec;
	std::filesystem::remove_all(to_long_path(path_), ec);
	path_.clear();
}

} // namespace recto::test
