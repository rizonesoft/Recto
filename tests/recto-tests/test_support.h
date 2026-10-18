#pragma once

#include <gtest/gtest.h>

#include <array>
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

// Writes all bytes to a file on disk using temp-file-then-replace in the
// destination folder, creating parent directories if needed (supporting
// extended-length paths).
void write_file_bytes(const std::filesystem::path& path, std::span<const uint8_t> data);

enum class WriteFailStage {
	None,
	AfterAcquire,
	AfterWrite,
};

// Arms a one-shot failure injection for the next write_file_bytes call at the
// requested stage (immediately after CreateFileW handle acquisition or after
// writing bytes before FlushFileBuffers/CloseHandle).
void fail_next_write_for_testing(WriteFailStage stage = WriteFailStage::AfterAcquire) noexcept;

// Arms a one-shot failure injection in TempDir::TempDir() immediately after
// CreateDirectoryW succeeds, recording the created path in *out_created_path
// before CreateDirectoryW and throwing after acquisition to test rollback.
void fail_next_temp_dir_for_testing(std::filesystem::path* out_created_path) noexcept;

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

// Structured result of comparing two 8-bit 4-channel RGBA buffers.
struct PixelDiffResult {
	bool matches{true};
	size_t mismatch_count{0};
	uint32_t first_x{0};
	uint32_t first_y{0};
	uint8_t max_delta_observed{0};
	std::array<uint8_t, 4> actual_rgba{0, 0, 0, 0};
	std::array<uint8_t, 4> expected_rgba{0, 0, 0, 0};
};

// Compares two RGBA pixel buffers of size width x height with arbitrary row
// strides (in bytes; 0 defaults to width * 4), ignoring row-padding bytes
// beyond width * 4 in each row. A pixel matches when all 4 channels differ by
// at most max_channel_delta.
PixelDiffResult compare_rgba(
	std::span<const uint8_t> actual,
	std::span<const uint8_t> expected,
	uint32_t width,
	uint32_t height,
	size_t actual_stride = 0,
	size_t expected_stride = 0,
	uint8_t max_channel_delta = 0);

// Formats a diagnostic string describing the first differing pixel and total
// mismatch count.
std::string format_rgba_diff(const PixelDiffResult& diff);

// Compares two RGBA pixel buffers and records a GoogleTest non-fatal failure
// naming (first_x, first_y) and expected vs actual [R, G, B, A] bytes when
// they differ beyond max_channel_delta.
PixelDiffResult expect_rgba_equal(
	std::span<const uint8_t> actual,
	std::span<const uint8_t> expected,
	uint32_t width,
	uint32_t height,
	size_t actual_stride = 0,
	size_t expected_stride = 0,
	uint8_t max_channel_delta = 0);

// Computes the exclusive block endpoint [start, end) of size at most
// block_size within [0, extent) without unsigned addition overflow.
constexpr uint32_t next_block_end(
	uint32_t start,
	uint32_t extent,
	uint32_t block_size = 8u) noexcept
{
	return (start >= extent)
		? extent
		: ((extent - start <= block_size) ? extent : (start + block_size));
}

// Computes the structural similarity index (SSIM in [0.0, 1.0]) over
// luminance and RGB channels between two RGBA buffers of size width x height
// with arbitrary row strides (0 defaults to width * 4), ignoring row-padding
// bytes beyond width * 4.
double compute_ssim(
	std::span<const uint8_t> actual,
	std::span<const uint8_t> expected,
	uint32_t width,
	uint32_t height,
	size_t actual_stride = 0,
	size_t expected_stride = 0);

// Computes the lowercase 64-character hexadecimal SHA-256 digest of a byte
// span using Windows CNG (bcrypt.dll, BCRYPT_SHA256_ALGORITHM).
std::string sha256_hex(std::span<const uint8_t> data);

// Computes the lowercase 64-character hexadecimal SHA-256 digest of a file on
// disk using Windows CNG (supporting extended-length paths).
std::string sha256_file(const std::filesystem::path& path);

namespace detail {

template <typename E>
constexpr decltype(auto) extract_error_code(const E& err) noexcept
{
	if constexpr (requires { err.code; }) {
		return (err.code);
	} else {
		return (err);
	}
}

template <typename E>
std::string format_error_detail(const E& err)
{
	if constexpr (requires { std::string_view(err.message); }) {
		return std::string(std::string_view(err.message));
	} else if constexpr (requires { std::to_string(err); }) {
		return std::to_string(err);
	} else {
		return "unexpected error";
	}
}

} // namespace detail

} // namespace recto::test

#define RECTO_ASSERT_OK(expr)                                                            \
	do {                                                                                 \
		auto&& _recto_assert_ok_res = (expr);                                            \
		if (!_recto_assert_ok_res.has_value()) {                                         \
			FAIL() << "Expected OK result for (" #expr "), got error: "                  \
				   << ::recto::test::detail::format_error_detail(                        \
						  _recto_assert_ok_res.error());                                 \
		}                                                                                \
	} while (0)

#define RECTO_EXPECT_ERR(expr, expected_code)                                            \
	do {                                                                                 \
		auto&& _recto_expect_err_res = (expr);                                           \
		if (_recto_expect_err_res.has_value()) {                                         \
			ADD_FAILURE() << "Expected error result (" #expected_code ") for (" #expr    \
							 "), but it succeeded";                                      \
		} else {                                                                         \
			EXPECT_EQ(                                                                   \
				::recto::test::detail::extract_error_code(_recto_expect_err_res.error()),\
				(expected_code))                                                         \
				<< "Mismatched error code for (" #expr ")";                              \
		}                                                                                \
	} while (0)
