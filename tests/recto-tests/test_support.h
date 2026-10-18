#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace recto::test {

// Resolves the repository root directory by walking upward from the
// running test executable path (GetModuleFileNameW(nullptr, ...)) until
// Recto.sln or tests/recto-tests is found, independent of the process
// current working directory.
std::filesystem::path repo_root();

// Resolves a path inside the test corpus directory. If the environment
// variable RECTO_CORPUS_DIR is set and non-empty, resolves relative to
// that directory; otherwise resolves relative to repo_root() / "tests" / "corpus".
std::filesystem::path corpus_path(std::string_view relative = {});

// Converts a filesystem path to a Windows extended-length path (\\?\)
// when needed so paths longer than MAX_PATH (260 characters) work
// reliably with Win32 and std::filesystem operations.
std::filesystem::path to_long_path(const std::filesystem::path& path);

// Reads all bytes from a file on disk (supporting extended-length paths).
std::vector<uint8_t> read_file_bytes(const std::filesystem::path& path);

// Writes all bytes to a file on disk, creating parent directories if needed
// (supporting extended-length paths).
void write_file_bytes(const std::filesystem::path& path, std::span<const uint8_t> data);

// RAII temporary directory under GetTempPathW() named
// "recto-test-<pid>-<tick>-<counter>". Clears FILE_ATTRIBUTE_READONLY on
// any contained files and subdirectories before removing the tree on
// scope exit without throwing.
class TempDir {
public:
	TempDir();
	~TempDir() noexcept;

	TempDir(const TempDir&) = delete;
	TempDir& operator=(const TempDir&) = delete;

	TempDir(TempDir&& other) noexcept;
	TempDir& operator=(TempDir&& other) noexcept;

	const std::filesystem::path& path() const noexcept { return path_; }
	std::filesystem::path file_path(std::string_view relative) const;

private:
	void cleanup() noexcept;
	std::filesystem::path path_;
};

} // namespace recto::test
