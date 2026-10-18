#include "test_support.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace recto::test {

namespace {

std::atomic<uint64_t> g_temp_dir_counter{1};
std::atomic<WriteFailStage> g_fail_next_write{WriteFailStage::None};
std::atomic<std::filesystem::path*> g_fail_next_temp_dir_out{nullptr};

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

struct TempDirGuard {
	std::filesystem::path path;
	bool armed{false};
	bool committed{false};

	TempDirGuard() noexcept = default;
	~TempDirGuard() noexcept
	{
		if (armed && !committed && !path.empty()) {
			RemoveDirectoryW(path.c_str());
		}
	}

	TempDirGuard(const TempDirGuard&) = delete;
	TempDirGuard& operator=(const TempDirGuard&) = delete;
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

void fail_next_temp_dir_for_testing(std::filesystem::path* out_created_path) noexcept
{
	g_fail_next_temp_dir_out.store(out_created_path, std::memory_order_relaxed);
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

	std::filesystem::path* const fail_out =
		g_fail_next_temp_dir_out.exchange(nullptr, std::memory_order_relaxed);
	const DWORD pid = GetCurrentProcessId();
	TempDirGuard dir_guard;
	for (int attempt = 0; attempt < 64; ++attempt) {
		LARGE_INTEGER qpc{};
		QueryPerformanceCounter(&qpc);
		const uint64_t seq = g_temp_dir_counter.fetch_add(1, std::memory_order_relaxed);
		const std::wstring dir_name = L"recto-test-" + std::to_wstring(pid) + L"-" +
			std::to_wstring(static_cast<uint64_t>(qpc.QuadPart)) + L"-" + std::to_wstring(seq);
		std::filesystem::path candidate = (base_temp / dir_name).lexically_normal();
		dir_guard.path = to_long_path(candidate);
		if (fail_out != nullptr) {
			*fail_out = dir_guard.path;
		}
		if (CreateDirectoryW(dir_guard.path.c_str(), nullptr) != 0) {
			dir_guard.armed = true;
			if (fail_out != nullptr) {
				throw std::runtime_error("TempDir: injected failure after CreateDirectoryW");
			}
			path_ = std::move(candidate);
			dir_guard.committed = true;
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

namespace {

struct ValidatedRgbaStrides {
	size_t row_bytes{0};
	size_t actual_stride{0};
	size_t expected_stride{0};
};

ValidatedRgbaStrides validate_rgba_buffers(
	std::span<const uint8_t> actual,
	std::span<const uint8_t> expected,
	uint32_t width,
	uint32_t height,
	size_t actual_stride,
	size_t expected_stride)
{
	if (width == 0 || height == 0) {
		throw std::invalid_argument("RGBA dimensions must be non-zero");
	}
	if (static_cast<size_t>(width) > SIZE_MAX / 4u) {
		throw std::invalid_argument("RGBA width overflows row byte count");
	}
	const size_t row_bytes = static_cast<size_t>(width) * 4u;
	const size_t eff_actual_stride = (actual_stride == 0) ? row_bytes : actual_stride;
	const size_t eff_expected_stride = (expected_stride == 0) ? row_bytes : expected_stride;
	if (eff_actual_stride < row_bytes || eff_expected_stride < row_bytes) {
		throw std::invalid_argument("RGBA row stride is smaller than width * 4");
	}

	const size_t last_row = static_cast<size_t>(height) - 1u;
	if (last_row > 0) {
		if (eff_actual_stride > (SIZE_MAX - row_bytes) / last_row ||
			eff_expected_stride > (SIZE_MAX - row_bytes) / last_row) {
			throw std::invalid_argument("RGBA buffer size overflows size_t");
		}
	}
	const size_t min_actual_bytes = last_row * eff_actual_stride + row_bytes;
	const size_t min_expected_bytes = last_row * eff_expected_stride + row_bytes;
	if (actual.size() < min_actual_bytes || expected.size() < min_expected_bytes) {
		throw std::invalid_argument("RGBA buffer is smaller than required for dimensions and stride");
	}
	return ValidatedRgbaStrides{row_bytes, eff_actual_stride, eff_expected_stride};
}

struct UniqueAlgHandle {
	BCRYPT_ALG_HANDLE handle{nullptr};

	UniqueAlgHandle() noexcept = default;
	~UniqueAlgHandle() noexcept
	{
		if (handle != nullptr) {
			BCryptCloseAlgorithmProvider(handle, 0);
		}
	}

	UniqueAlgHandle(const UniqueAlgHandle&) = delete;
	UniqueAlgHandle& operator=(const UniqueAlgHandle&) = delete;
};

struct UniqueHashHandle {
	BCRYPT_HASH_HANDLE handle{nullptr};

	UniqueHashHandle() noexcept = default;
	~UniqueHashHandle() noexcept
	{
		if (handle != nullptr) {
			BCryptDestroyHash(handle);
		}
	}

	UniqueHashHandle(const UniqueHashHandle&) = delete;
	UniqueHashHandle& operator=(const UniqueHashHandle&) = delete;
};

std::string bytes_to_hex(std::span<const uint8_t> bytes)
{
	static constexpr char kHexDigits[] = "0123456789abcdef";
	std::string out;
	out.resize(bytes.size() * 2u);
	for (size_t i = 0; i < bytes.size(); ++i) {
		const uint8_t b = bytes[i];
		out[i * 2u] = kHexDigits[(b >> 4) & 0x0Fu];
		out[i * 2u + 1u] = kHexDigits[b & 0x0Fu];
	}
	return out;
}

} // namespace

PixelDiffResult compare_rgba(
	std::span<const uint8_t> actual,
	std::span<const uint8_t> expected,
	uint32_t width,
	uint32_t height,
	size_t actual_stride,
	size_t expected_stride,
	uint8_t max_channel_delta)
{
	const auto strides = validate_rgba_buffers(
		actual, expected, width, height, actual_stride, expected_stride);

	PixelDiffResult result{};
	for (uint32_t y = 0; y < height; ++y) {
		const uint8_t* actual_row = actual.data() + static_cast<size_t>(y) * strides.actual_stride;
		const uint8_t* expected_row = expected.data() + static_cast<size_t>(y) * strides.expected_stride;
		for (uint32_t x = 0; x < width; ++x) {
			const uint8_t* a = actual_row + static_cast<size_t>(x) * 4u;
			const uint8_t* e = expected_row + static_cast<size_t>(x) * 4u;

			uint8_t pixel_max_delta = 0;
			for (size_t c = 0; c < 4u; ++c) {
				const uint8_t delta = static_cast<uint8_t>(
					(a[c] >= e[c]) ? (a[c] - e[c]) : (e[c] - a[c]));
				pixel_max_delta = std::max(pixel_max_delta, delta);
			}
			result.max_delta_observed = std::max(result.max_delta_observed, pixel_max_delta);

			if (pixel_max_delta > max_channel_delta) {
				if (result.mismatch_count == 0) {
					result.matches = false;
					result.first_x = x;
					result.first_y = y;
					result.actual_rgba = {a[0], a[1], a[2], a[3]};
					result.expected_rgba = {e[0], e[1], e[2], e[3]};
				}
				++result.mismatch_count;
			}
		}
	}
	return result;
}

std::string format_rgba_diff(const PixelDiffResult& diff)
{
	if (diff.matches) {
		return "RGBA buffers match (max_delta = " +
			std::to_string(static_cast<unsigned>(diff.max_delta_observed)) + ")";
	}
	std::ostringstream oss;
	oss << "RGBA mismatch at (" << diff.first_x << ", " << diff.first_y << "): expected ["
		<< static_cast<unsigned>(diff.expected_rgba[0]) << ", "
		<< static_cast<unsigned>(diff.expected_rgba[1]) << ", "
		<< static_cast<unsigned>(diff.expected_rgba[2]) << ", "
		<< static_cast<unsigned>(diff.expected_rgba[3]) << "], actual ["
		<< static_cast<unsigned>(diff.actual_rgba[0]) << ", "
		<< static_cast<unsigned>(diff.actual_rgba[1]) << ", "
		<< static_cast<unsigned>(diff.actual_rgba[2]) << ", "
		<< static_cast<unsigned>(diff.actual_rgba[3])
		<< "], delta = " << static_cast<unsigned>(diff.max_delta_observed)
		<< ", mismatch_count = " << diff.mismatch_count;
	return oss.str();
}

PixelDiffResult expect_rgba_equal(
	std::span<const uint8_t> actual,
	std::span<const uint8_t> expected,
	uint32_t width,
	uint32_t height,
	size_t actual_stride,
	size_t expected_stride,
	uint8_t max_channel_delta)
{
	const PixelDiffResult diff = compare_rgba(
		actual, expected, width, height, actual_stride, expected_stride, max_channel_delta);
	if (!diff.matches) {
		ADD_FAILURE() << format_rgba_diff(diff);
	}
	return diff;
}

double compute_ssim(
	std::span<const uint8_t> actual,
	std::span<const uint8_t> expected,
	uint32_t width,
	uint32_t height,
	size_t actual_stride,
	size_t expected_stride)
{
	const auto strides = validate_rgba_buffers(
		actual, expected, width, height, actual_stride, expected_stride);

	// Standard 8-bit SSIM constants: C1 = (0.01 * 255)^2, C2 = (0.03 * 255)^2.
	constexpr double kC1 = 6.5025;
	constexpr double kC2 = 58.5225;
	constexpr uint32_t kBlockSize = 8;

	auto channel_value = [](const uint8_t* px, int ch) noexcept -> double {
		if (ch == 0) {
			return 0.299 * static_cast<double>(px[0]) +
				0.587 * static_cast<double>(px[1]) +
				0.114 * static_cast<double>(px[2]);
		}
		return static_cast<double>(px[ch - 1]);
	};

	double total_ssim = 0.0;
	size_t window_count = 0;

	for (uint32_t y0 = 0; y0 < height; y0 += kBlockSize) {
		const uint32_t y1 = std::min(height, y0 + kBlockSize);
		for (uint32_t x0 = 0; x0 < width; x0 += kBlockSize) {
			const uint32_t x1 = std::min(width, x0 + kBlockSize);
			const double n = static_cast<double>((x1 - x0) * (y1 - y0));

			// Compute SSIM across luminance (ch=0) and R, G, B, A (ch=1..4).
			double block_ssim_sum = 0.0;
			for (int ch = 0; ch < 5; ++ch) {
				double sum_a = 0.0;
				double sum_e = 0.0;
				bool identical = true;
				for (uint32_t y = y0; y < y1; ++y) {
					const uint8_t* a_row = actual.data() + static_cast<size_t>(y) * strides.actual_stride;
					const uint8_t* e_row = expected.data() + static_cast<size_t>(y) * strides.expected_stride;
					for (uint32_t x = x0; x < x1; ++x) {
						const uint8_t* a_px = a_row + static_cast<size_t>(x) * 4u;
						const uint8_t* e_px = e_row + static_cast<size_t>(x) * 4u;
						const double va = channel_value(a_px, ch);
						const double ve = channel_value(e_px, ch);
						if (va != ve) {
							identical = false;
						}
						sum_a += va;
						sum_e += ve;
					}
				}
				if (identical) {
					block_ssim_sum += 1.0;
					continue;
				}
				const double mean_a = sum_a / n;
				const double mean_e = sum_e / n;

				double var_a = 0.0;
				double var_e = 0.0;
				double cov_ae = 0.0;
				for (uint32_t y = y0; y < y1; ++y) {
					const uint8_t* a_row = actual.data() + static_cast<size_t>(y) * strides.actual_stride;
					const uint8_t* e_row = expected.data() + static_cast<size_t>(y) * strides.expected_stride;
					for (uint32_t x = x0; x < x1; ++x) {
						const uint8_t* a_px = a_row + static_cast<size_t>(x) * 4u;
						const uint8_t* e_px = e_row + static_cast<size_t>(x) * 4u;
						const double da = channel_value(a_px, ch) - mean_a;
						const double de = channel_value(e_px, ch) - mean_e;
						var_a += da * da;
						var_e += de * de;
						cov_ae += da * de;
					}
				}
				var_a /= n;
				var_e /= n;
				cov_ae /= n;

				const double num = (2.0 * mean_a * mean_e + kC1) * (2.0 * cov_ae + kC2);
				const double den = (mean_a * mean_a + mean_e * mean_e + kC1) * (var_a + var_e + kC2);
				const double ch_ssim = (den > 0.0) ? (num / den) : 1.0;
				block_ssim_sum += ch_ssim;
			}
			total_ssim += (block_ssim_sum / 5.0);
			++window_count;
		}
	}

	if (window_count == 0) {
		return 1.0;
	}
	return std::clamp(total_ssim / static_cast<double>(window_count), 0.0, 1.0);
}

std::string sha256_hex(std::span<const uint8_t> data)
{
	UniqueAlgHandle alg;
	BCRYPT_ALG_HANDLE raw_alg = nullptr;
	if (BCryptOpenAlgorithmProvider(&raw_alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
		throw std::runtime_error("sha256_hex: BCryptOpenAlgorithmProvider failed");
	}
	alg.handle = raw_alg;

	UniqueHashHandle hash;
	BCRYPT_HASH_HANDLE raw_hash = nullptr;
	if (BCryptCreateHash(alg.handle, &raw_hash, nullptr, 0, nullptr, 0, 0) < 0) {
		throw std::runtime_error("sha256_hex: BCryptCreateHash failed");
	}
	hash.handle = raw_hash;

	size_t offset = 0;
	while (offset < data.size()) {
		const ULONG chunk = static_cast<ULONG>(
			std::min<size_t>(data.size() - offset, 1u << 20));
		if (BCryptHashData(
				hash.handle,
				const_cast<PUCHAR>(data.data() + offset),
				chunk,
				0) < 0) {
			throw std::runtime_error("sha256_hex: BCryptHashData failed");
		}
		offset += chunk;
	}

	std::array<uint8_t, 32> digest{};
	if (BCryptFinishHash(hash.handle, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
		throw std::runtime_error("sha256_hex: BCryptFinishHash failed");
	}
	return bytes_to_hex(digest);
}

std::string sha256_file(const std::filesystem::path& path)
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
		throw std::runtime_error("sha256_file: CreateFileW failed");
	}

	UniqueAlgHandle alg;
	BCRYPT_ALG_HANDLE raw_alg = nullptr;
	if (BCryptOpenAlgorithmProvider(&raw_alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
		throw std::runtime_error("sha256_file: BCryptOpenAlgorithmProvider failed");
	}
	alg.handle = raw_alg;

	UniqueHashHandle hash;
	BCRYPT_HASH_HANDLE raw_hash = nullptr;
	if (BCryptCreateHash(alg.handle, &raw_hash, nullptr, 0, nullptr, 0, 0) < 0) {
		throw std::runtime_error("sha256_file: BCryptCreateHash failed");
	}
	hash.handle = raw_hash;

	std::array<uint8_t, 65536> buffer{};
	for (;;) {
		DWORD bytes_read = 0;
		if (!ReadFile(
				file.handle,
				buffer.data(),
				static_cast<DWORD>(buffer.size()),
				&bytes_read,
				nullptr)) {
			throw std::runtime_error("sha256_file: ReadFile failed");
		}
		if (bytes_read == 0) {
			break;
		}
		if (BCryptHashData(hash.handle, buffer.data(), bytes_read, 0) < 0) {
			throw std::runtime_error("sha256_file: BCryptHashData failed");
		}
	}

	std::array<uint8_t, 32> digest{};
	if (BCryptFinishHash(hash.handle, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
		throw std::runtime_error("sha256_file: BCryptFinishHash failed");
	}
	return bytes_to_hex(digest);
}

} // namespace recto::test
