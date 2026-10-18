#include "test_support.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#endif

#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace {

class ScopedCurrentDir {
public:
	explicit ScopedCurrentDir(const std::filesystem::path& target)
	{
		const DWORD needed = GetCurrentDirectoryW(0, nullptr);
		if (needed == 0) {
			throw std::runtime_error("GetCurrentDirectoryW sizing failed");
		}
		std::wstring buf(needed, L'\0');
		const DWORD copied = GetCurrentDirectoryW(needed, buf.data());
		if (copied == 0 || copied >= needed) {
			throw std::runtime_error("GetCurrentDirectoryW copy failed");
		}
		buf.resize(copied);
		original_ = std::move(buf);
		if (SetCurrentDirectoryW(target.c_str()) == 0) {
			const auto long_target = recto::test::to_long_path(target);
			if (SetCurrentDirectoryW(long_target.c_str()) == 0) {
				throw std::runtime_error("SetCurrentDirectoryW failed");
			}
		}
	}
	~ScopedCurrentDir() noexcept
	{
		if (!original_.empty()) {
			BOOL restored = SetCurrentDirectoryW(original_.c_str());
			if (restored == 0) {
				try {
					const auto long_original = recto::test::to_long_path(original_);
					restored = SetCurrentDirectoryW(long_original.c_str());
				} catch (...) {
				}
			}
			EXPECT_NE(restored, 0) << "Failed to restore current directory in ~ScopedCurrentDir";
		}
	}

	ScopedCurrentDir(const ScopedCurrentDir&) = delete;
	ScopedCurrentDir& operator=(const ScopedCurrentDir&) = delete;

private:
	std::wstring original_;
};

class ScopedEnvVar {
public:
	ScopedEnvVar(const wchar_t* name, const wchar_t* value) : name_(name)
	{
		const DWORD len = GetEnvironmentVariableW(name, nullptr, 0);
		if (len > 0) {
			had_original_ = true;
			original_.resize(len - 1);
			GetEnvironmentVariableW(name, original_.data(), len);
		}
		SetEnvironmentVariableW(name, value);
	}
	~ScopedEnvVar() noexcept
	{
		if (had_original_) {
			SetEnvironmentVariableW(name_.c_str(), original_.c_str());
		} else {
			SetEnvironmentVariableW(name_.c_str(), nullptr);
		}
	}

	ScopedEnvVar(const ScopedEnvVar&) = delete;
	ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;

private:
	std::wstring name_;
	bool had_original_{false};
	std::wstring original_;
};

} // namespace

TEST(Support, CorpusPathResolution)
{
	const auto root1 = recto::test::repo_root();
	ASSERT_FALSE(root1.empty());
	EXPECT_TRUE(std::filesystem::exists(root1 / "Recto.sln"));
	EXPECT_TRUE(std::filesystem::exists(root1 / "LICENSE"));

	std::filesystem::path temp1_root;
	std::filesystem::path temp2_root;
	{
		recto::test::TempDir temp;
		temp1_root = temp.path();

		std::string long_rel_dir;
		for (int i = 0; i < 6; ++i) {
			long_rel_dir += "cwd_segment_abcdefghijklmnopqrstuvwxyz_012345/";
		}
		const auto long_cwd = temp.file_path(long_rel_dir);
		EXPECT_GT(long_cwd.native().size(), 280u);
		std::error_code ec;
		std::filesystem::create_directories(recto::test::to_long_path(long_cwd), ec);
		ASSERT_FALSE(ec);

		{
			ScopedCurrentDir outer_cwd_guard(long_cwd);
			const auto root2 = recto::test::repo_root();
			EXPECT_EQ(root1, root2);

			{
				recto::test::TempDir inner_temp;
				temp2_root = inner_temp.path();
				{
					// Exercise ScopedCurrentDir saving and restoring a >280-char original directory.
					ScopedCurrentDir inner_cwd_guard(inner_temp.path());
					EXPECT_EQ(recto::test::repo_root(), root1);
				}
			}
			EXPECT_FALSE(std::filesystem::exists(recto::test::to_long_path(temp2_root)));

			ScopedEnvVar clear_env(L"RECTO_CORPUS_DIR", nullptr);
			const auto default_corpus = recto::test::corpus_path("render/simple-1p.pdf");
			EXPECT_EQ(default_corpus, (root1 / "tests" / "corpus" / "render" / "simple-1p.pdf").lexically_normal());
		}

		{
			ScopedEnvVar override_env(L"RECTO_CORPUS_DIR", temp.path().c_str());
			const auto redirected = recto::test::corpus_path("a.pdf");
			EXPECT_EQ(redirected, (temp.path() / "a.pdf").lexically_normal());
		}
	}
	EXPECT_FALSE(std::filesystem::exists(recto::test::to_long_path(temp1_root)));
}

TEST(Support, TempDirCleanup)
{
	std::filesystem::path single_dir;
	{
		recto::test::TempDir temp;
		single_dir = temp.path();
		ASSERT_TRUE(std::filesystem::exists(single_dir));
		const std::vector<uint8_t> payload{'r', 'e', 'c', 't', 'o'};
		const auto target_file = temp.file_path("nested/sample.bin");
		recto::test::write_file_bytes(target_file, payload);
		EXPECT_EQ(recto::test::read_file_bytes(target_file), payload);

		const std::vector<uint8_t> replacement{'a', 't', 'o', 'm', 'i', 'c'};
		recto::test::write_file_bytes(target_file, replacement);
		EXPECT_EQ(recto::test::read_file_bytes(target_file), replacement);

		// Failure-injection regression tests at both the handle-acquisition boundary
		// and the post-write boundary: destination bytes remain unchanged and no
		// temporary file is left behind in the destination directory.
		const std::vector<uint8_t> aborted{'a', 'b', 'o', 'r', 't'};
		for (const auto stage : {
				 recto::test::WriteFailStage::AfterAcquire,
				 recto::test::WriteFailStage::AfterWrite,
			 }) {
			recto::test::fail_next_write_for_testing(stage);
			EXPECT_THROW(recto::test::write_file_bytes(target_file, aborted), std::runtime_error);
			EXPECT_EQ(recto::test::read_file_bytes(target_file), replacement);

			size_t entry_count = 0;
			for (const auto& entry :
				 std::filesystem::directory_iterator(recto::test::to_long_path(target_file.parent_path()))) {
				++entry_count;
				EXPECT_EQ(entry.path().filename(), std::filesystem::path("sample.bin"));
			}
			EXPECT_EQ(entry_count, 1u);
		}
	}
	EXPECT_FALSE(std::filesystem::exists(single_dir));

	// Constructor-rollback regression test: if TempDir::TempDir() fails after
	// CreateDirectoryW succeeds, the newly created directory is removed before
	// the exception escapes the constructor.
	std::filesystem::path aborted_temp_dir;
	recto::test::fail_next_temp_dir_for_testing(&aborted_temp_dir);
	EXPECT_THROW({ recto::test::TempDir rollback_dir; }, std::runtime_error);
	EXPECT_FALSE(aborted_temp_dir.empty());
	EXPECT_FALSE(std::filesystem::exists(aborted_temp_dir));

	constexpr size_t kThreadCount = 32;
	std::vector<std::filesystem::path> created_paths(kThreadCount);
	{
		std::vector<recto::test::TempDir> dirs(kThreadCount);
		std::vector<std::thread> threads;
		threads.reserve(kThreadCount);
		for (size_t i = 0; i < kThreadCount; ++i) {
			threads.emplace_back([&, i]() {
				recto::test::TempDir local_dir;
				created_paths[i] = local_dir.path();
				const std::vector<uint8_t> data{static_cast<uint8_t>(i)};
				recto::test::write_file_bytes(local_dir.file_path("thread.bin"), data);
				dirs[i] = std::move(local_dir);
			});
		}
		for (auto& t : threads) {
			t.join();
		}

		std::set<std::filesystem::path> unique_paths(created_paths.begin(), created_paths.end());
		EXPECT_EQ(unique_paths.size(), kThreadCount);
		for (const auto& p : created_paths) {
			EXPECT_TRUE(std::filesystem::exists(p));
		}
	}
	for (const auto& p : created_paths) {
		EXPECT_FALSE(std::filesystem::exists(p));
	}
}

TEST(Support, TempDirLongPathAndReadOnly)
{
	std::filesystem::path root_path;
	std::filesystem::path long_file;
	{
		recto::test::TempDir temp;
		root_path = temp.path();

		std::string deep_rel;
		for (int i = 0; i < 6; ++i) {
			deep_rel += "segment_abcdefghijklmnopqrstuvwxyz_0123456789/";
		}
		deep_rel += "readonly_long_target_file.bin";

		long_file = temp.file_path(deep_rel);
		EXPECT_GT(long_file.native().size(), 280u);

		const std::vector<uint8_t> expected{0x25, 0x50, 0x44, 0x46, 0x2D, 0x31, 0x2E, 0x37};
		recto::test::write_file_bytes(long_file, expected);
		const auto actual = recto::test::read_file_bytes(long_file);
		EXPECT_EQ(actual, expected);

		// Near-limit (250-character) single filename component regression test.
		const std::string near_limit_name = std::string(246, 'f') + ".bin";
		ASSERT_EQ(near_limit_name.size(), 250u);
		const auto near_limit_file = temp.file_path("near_limit/" + near_limit_name);
		recto::test::write_file_bytes(near_limit_file, expected);
		EXPECT_EQ(recto::test::read_file_bytes(near_limit_file), expected);

		const auto extended = recto::test::to_long_path(long_file);
		const DWORD attrs = GetFileAttributesW(extended.c_str());
		ASSERT_NE(attrs, INVALID_FILE_ATTRIBUTES);
		ASSERT_NE(SetFileAttributesW(extended.c_str(), attrs | FILE_ATTRIBUTE_READONLY), 0);
	}
	EXPECT_FALSE(std::filesystem::exists(recto::test::to_long_path(long_file)));
	EXPECT_FALSE(std::filesystem::exists(recto::test::to_long_path(root_path)));
}

TEST(Support, PixelComparison)
{
	constexpr uint32_t kWidth = 64;
	constexpr uint32_t kHeight = 64;
	std::vector<uint8_t> expected(kWidth * kHeight * 4u, 0);
	for (uint32_t y = 0; y < kHeight; ++y) {
		for (uint32_t x = 0; x < kWidth; ++x) {
			const size_t idx = (static_cast<size_t>(y) * kWidth + x) * 4u;
			expected[idx + 0] = static_cast<uint8_t>((x * 3u) & 0xFFu);
			expected[idx + 1] = static_cast<uint8_t>((y * 5u) & 0xFFu);
			expected[idx + 2] = static_cast<uint8_t>((x + y) & 0xFFu);
			expected[idx + 3] = 255u;
		}
	}
	std::vector<uint8_t> actual = expected;

	const auto equal_diff = recto::test::expect_rgba_equal(actual, expected, kWidth, kHeight);
	EXPECT_TRUE(equal_diff.matches);
	EXPECT_EQ(equal_diff.mismatch_count, 0u);
	EXPECT_EQ(equal_diff.max_delta_observed, 0u);

	// Perturb channel G by +1 at exact coordinate (17, 42).
	const size_t target_idx = (42u * kWidth + 17u) * 4u;
	expected[target_idx + 0] = 10u;
	expected[target_idx + 1] = 100u;
	expected[target_idx + 2] = 200u;
	expected[target_idx + 3] = 255u;
	actual[target_idx + 0] = 10u;
	actual[target_idx + 1] = 101u;
	actual[target_idx + 2] = 200u;
	actual[target_idx + 3] = 255u;

	const auto diff = recto::test::compare_rgba(actual, expected, kWidth, kHeight);
	EXPECT_FALSE(diff.matches);
	EXPECT_EQ(diff.mismatch_count, 1u);
	EXPECT_EQ(diff.first_x, 17u);
	EXPECT_EQ(diff.first_y, 42u);
	EXPECT_EQ(diff.max_delta_observed, 1u);
	EXPECT_EQ(diff.expected_rgba, (std::array<uint8_t, 4>{10u, 100u, 200u, 255u}));
	EXPECT_EQ(diff.actual_rgba, (std::array<uint8_t, 4>{10u, 101u, 200u, 255u}));

	const std::string formatted = recto::test::format_rgba_diff(diff);
	EXPECT_NE(formatted.find("(17, 42)"), std::string::npos);
	EXPECT_NE(formatted.find("expected [10, 100, 200, 255]"), std::string::npos);
	EXPECT_NE(formatted.find("actual [10, 101, 200, 255]"), std::string::npos);
	EXPECT_NE(formatted.find("delta = 1"), std::string::npos);
	EXPECT_NE(formatted.find("mismatch_count = 1"), std::string::npos);

	EXPECT_NONFATAL_FAILURE(
		recto::test::expect_rgba_equal(actual, expected, kWidth, kHeight),
		"RGBA mismatch at (17, 42): expected [10, 100, 200, 255], actual [10, 101, 200, 255], delta = 1, mismatch_count = 1");

	// Within max_channel_delta = 1 tolerance, the buffers match.
	const auto tolerated = recto::test::expect_rgba_equal(actual, expected, kWidth, kHeight, 0, 0, 1);
	EXPECT_TRUE(tolerated.matches);
	EXPECT_EQ(tolerated.mismatch_count, 0u);
	EXPECT_EQ(tolerated.max_delta_observed, 1u);

	EXPECT_THROW(recto::test::compare_rgba(actual, expected, 0, kHeight), std::invalid_argument);
	std::vector<uint8_t> short_buf(16, 0);
	EXPECT_THROW(recto::test::compare_rgba(short_buf, expected, kWidth, kHeight), std::invalid_argument);
}

TEST(Support, PixelStridePadding)
{
	constexpr uint32_t kWidth = 37;
	constexpr uint32_t kHeight = 19;
	constexpr size_t kTightStride = kWidth * 4u;
	constexpr size_t kPaddedStride = kTightStride + 64u;

	std::vector<uint8_t> expected(kHeight * kTightStride, 0);
	std::vector<uint8_t> actual(kHeight * kPaddedStride, 0xDE);

	for (uint32_t y = 0; y < kHeight; ++y) {
		for (uint32_t x = 0; x < kWidth; ++x) {
			const uint8_t r = static_cast<uint8_t>((x * 7u + y) & 0xFFu);
			const uint8_t g = static_cast<uint8_t>((y * 11u + x) & 0xFFu);
			const uint8_t b = static_cast<uint8_t>((x ^ y) & 0xFFu);
			const uint8_t a = 255u;

			const size_t e_idx = static_cast<size_t>(y) * kTightStride + static_cast<size_t>(x) * 4u;
			expected[e_idx + 0] = r;
			expected[e_idx + 1] = g;
			expected[e_idx + 2] = b;
			expected[e_idx + 3] = a;

			const size_t a_idx = static_cast<size_t>(y) * kPaddedStride + static_cast<size_t>(x) * 4u;
			actual[a_idx + 0] = r;
			actual[a_idx + 1] = g;
			actual[a_idx + 2] = b;
			actual[a_idx + 3] = a;
		}
		// Fill padding bytes in actual with varying non-zero garbage.
		for (size_t pad = kTightStride; pad < kPaddedStride; ++pad) {
			actual[static_cast<size_t>(y) * kPaddedStride + pad] =
				static_cast<uint8_t>((y * 13u + pad) & 0xFFu);
		}
	}

	const auto res = recto::test::expect_rgba_equal(
		actual, expected, kWidth, kHeight, kPaddedStride, kTightStride);
	EXPECT_TRUE(res.matches);
	EXPECT_EQ(res.mismatch_count, 0u);
	EXPECT_EQ(res.max_delta_observed, 0u);

	EXPECT_THROW(
		recto::test::compare_rgba(actual, expected, kWidth, kHeight, kTightStride - 1u, kTightStride),
		std::invalid_argument);
}

TEST(Support, SsimCalculation)
{
	constexpr uint32_t kWidth = 64;
	constexpr uint32_t kHeight = 64;
	std::vector<uint8_t> base(kWidth * kHeight * 4u, 0);
	std::vector<uint8_t> inverted(kWidth * kHeight * 4u, 0);
	std::vector<uint8_t> near_copy(kWidth * kHeight * 4u, 0);

	for (uint32_t y = 0; y < kHeight; ++y) {
		for (uint32_t x = 0; x < kWidth; ++x) {
			const size_t idx = (static_cast<size_t>(y) * kWidth + x) * 4u;
			const uint8_t pattern = (((x / 4u) + (y / 4u)) % 2u == 0u) ? 20u : 235u;
			base[idx + 0] = pattern;
			base[idx + 1] = static_cast<uint8_t>((x * 4u) & 0xFFu);
			base[idx + 2] = static_cast<uint8_t>((y * 4u) & 0xFFu);
			base[idx + 3] = 255u;

			inverted[idx + 0] = static_cast<uint8_t>(255u - base[idx + 0]);
			inverted[idx + 1] = static_cast<uint8_t>(255u - base[idx + 1]);
			inverted[idx + 2] = static_cast<uint8_t>(255u - base[idx + 2]);
			inverted[idx + 3] = 255u;

			near_copy[idx + 0] = static_cast<uint8_t>(base[idx + 0] + 1u);
			near_copy[idx + 1] = base[idx + 1];
			near_copy[idx + 2] = base[idx + 2];
			near_copy[idx + 3] = 255u;
		}
	}

	EXPECT_DOUBLE_EQ(recto::test::compute_ssim(base, base, kWidth, kHeight), 1.0);

	const double inv_ssim = recto::test::compute_ssim(inverted, base, kWidth, kHeight);
	EXPECT_GE(inv_ssim, 0.0);
	EXPECT_LT(inv_ssim, 0.1);

	const double near_ssim = recto::test::compute_ssim(near_copy, base, kWidth, kHeight);
	EXPECT_GE(near_ssim, 0.99);
	EXPECT_LT(near_ssim, 1.0);

	// Opaque black vs opaque white must score < 0.1 without alpha inflating similarity.
	std::vector<uint8_t> opaque_black(kWidth * kHeight * 4u, 0u);
	std::vector<uint8_t> opaque_white(kWidth * kHeight * 4u, 255u);
	for (size_t i = 0; i < kWidth * kHeight; ++i) {
		opaque_black[i * 4u + 3u] = 255u;
	}
	const double bw_ssim =
		recto::test::compute_ssim(opaque_black, opaque_white, kWidth, kHeight);
	EXPECT_GE(bw_ssim, 0.0);
	EXPECT_LT(bw_ssim, 0.1);

	// Non-multiple-of-8 dimensions exercise partial blocks cleanly.
	constexpr uint32_t kOddW = 13u;
	constexpr uint32_t kOddH = 7u;
	std::vector<uint8_t> odd_black(kOddW * kOddH * 4u, 0u);
	std::vector<uint8_t> odd_white(kOddW * kOddH * 4u, 255u);
	for (size_t i = 0; i < kOddW * kOddH; ++i) {
		odd_black[i * 4u + 3u] = 255u;
	}
	EXPECT_DOUBLE_EQ(recto::test::compute_ssim(odd_black, odd_black, kOddW, kOddH), 1.0);
	EXPECT_LT(recto::test::compute_ssim(odd_black, odd_white, kOddW, kOddH), 0.1);

	// Block-bound helper must never wrap uint32_t near UINT32_MAX.
	EXPECT_EQ(recto::test::next_block_end(0u, 13u, 8u), 8u);
	EXPECT_EQ(recto::test::next_block_end(8u, 13u, 8u), 13u);
	EXPECT_EQ(recto::test::next_block_end(0xFFFFFFF0u, 0xFFFFFFFFu, 8u), 0xFFFFFFF8u);
	EXPECT_EQ(recto::test::next_block_end(0xFFFFFFF8u, 0xFFFFFFFFu, 8u), 0xFFFFFFFFu);
	EXPECT_EQ(recto::test::next_block_end(0xFFFFFFFAu, 0xFFFFFFFFu, 8u), 0xFFFFFFFFu);
	EXPECT_EQ(recto::test::next_block_end(0xFFFFFFFFu, 0xFFFFFFFFu, 8u), 0xFFFFFFFFu);
}

TEST(Support, Sha256Hex)
{
	const std::span<const uint8_t> empty_span{};
	EXPECT_EQ(
		recto::test::sha256_hex(empty_span),
		"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

	const std::array<uint8_t, 3> abc{'a', 'b', 'c'};
	EXPECT_EQ(
		recto::test::sha256_hex(abc),
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

	const std::string nist_448 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	EXPECT_EQ(
		recto::test::sha256_hex(std::span<const uint8_t>(
			reinterpret_cast<const uint8_t*>(nist_448.data()), nist_448.size())),
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

	// 1,000,000 'a' bytes NIST vector.
	const std::vector<uint8_t> million_a(1000000u, static_cast<uint8_t>('a'));
	EXPECT_EQ(
		recto::test::sha256_hex(million_a),
		"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

	const auto license_path = recto::test::repo_root() / "LICENSE";
	EXPECT_EQ(
		recto::test::sha256_file(license_path),
		"0c2b2577702544e6ca2110800c25129ef79a7277e74f888ae852afb90cb363b4");

	EXPECT_THROW(
		recto::test::sha256_file(recto::test::repo_root() / "nonexistent_file_for_sha256.bin"),
		std::runtime_error);
}

namespace {

enum class FakeErrorCode {
	Ok = 0,
	DamagedXref = 1,
	NeedsPassword = 2,
};

struct FakePdfError {
	FakeErrorCode code{FakeErrorCode::Ok};
	std::string message;
};

template <typename T, typename E = FakePdfError>
class FakeResult {
public:
	static FakeResult ok(T val) { return FakeResult(std::in_place_index<0>, std::move(val)); }
	static FakeResult err(E error) { return FakeResult(std::in_place_index<1>, std::move(error)); }

	bool has_value() const noexcept { return storage_.index() == 0; }
	T& value() { return std::get<0>(storage_); }
	const T& value() const { return std::get<0>(storage_); }
	const E& error() const { return std::get<1>(storage_); }

private:
	template <size_t I, typename Arg>
	FakeResult(std::in_place_index_t<I> idx, Arg&& arg) : storage_(idx, std::forward<Arg>(arg)) {}

	std::variant<T, E> storage_;
};

template <typename E>
class FakeResult<void, E> {
public:
	static FakeResult ok() { return FakeResult(std::in_place_index<0>, std::monostate{}); }
	static FakeResult err(E error) { return FakeResult(std::in_place_index<1>, std::move(error)); }

	bool has_value() const noexcept { return storage_.index() == 0; }
	void value() const {}
	const E& error() const { return std::get<1>(storage_); }

private:
	template <size_t I, typename Arg>
	FakeResult(std::in_place_index_t<I> idx, Arg&& arg) : storage_(idx, std::forward<Arg>(arg)) {}

	std::variant<std::monostate, E> storage_;
};

void trigger_assert_ok_failure()
{
	auto failing = FakeResult<int>::err(
		FakePdfError{FakeErrorCode::DamagedXref, "damaged xref table at offset 128"});
	RECTO_ASSERT_OK(failing);
}

} // namespace

TEST(Support, ResultMacros)
{
	auto copyable_ok = FakeResult<int>::ok(42);
	RECTO_ASSERT_OK(copyable_ok);
	EXPECT_EQ(copyable_ok.value(), 42);

	auto move_only_ok = FakeResult<std::unique_ptr<int>>::ok(std::make_unique<int>(99));
	RECTO_ASSERT_OK(move_only_ok);
	ASSERT_NE(move_only_ok.value(), nullptr);
	EXPECT_EQ(*move_only_ok.value(), 99);

	auto void_ok = FakeResult<void>::ok();
	RECTO_ASSERT_OK(void_ok);

	auto err_res = FakeResult<int>::err(
		FakePdfError{FakeErrorCode::NeedsPassword, "document is encrypted"});
	RECTO_EXPECT_ERR(err_res, FakeErrorCode::NeedsPassword);

	auto direct_code_err = FakeResult<int, FakeErrorCode>::err(FakeErrorCode::DamagedXref);
	RECTO_EXPECT_ERR(direct_code_err, FakeErrorCode::DamagedXref);

	EXPECT_FATAL_FAILURE(trigger_assert_ok_failure(), "damaged xref table at offset 128");
	EXPECT_NONFATAL_FAILURE(
		RECTO_EXPECT_ERR(copyable_ok, FakeErrorCode::DamagedXref),
		"Expected error result");
	EXPECT_NONFATAL_FAILURE(
		RECTO_EXPECT_ERR(err_res, FakeErrorCode::DamagedXref),
		"Mismatched error code");
}

namespace {

bool check_asan_env_expectation(bool asan_compiled, std::wstring_view expect_env) noexcept
{
	if (expect_env == L"1") {
		return asan_compiled;
	}
	if (expect_env == L"0") {
		return !asan_compiled;
	}
	return true;
}

std::wstring query_env_var(const wchar_t* name)
{
	const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
	if (needed <= 1) {
		return {};
	}
	std::wstring val(needed - 1, L'\0');
	const DWORD copied = GetEnvironmentVariableW(name, val.data(), needed);
	if (copied == 0 || copied >= needed) {
		return {};
	}
	return val;
}

} // namespace

TEST(Support, AsanActiveWhenEnabled)
{
#if defined(__SANITIZE_ADDRESS__)
	constexpr bool kAsanCompiled = true;
#else
	constexpr bool kAsanCompiled = false;
#endif

	const std::wstring expect_env = query_env_var(L"RECTO_EXPECT_ASAN");
	EXPECT_TRUE(check_asan_env_expectation(kAsanCompiled, expect_env))
		<< "RECTO_EXPECT_ASAN mismatch against compile-time __SANITIZE_ADDRESS__";

	// Verify expectation helper catches mismatches in both directions.
	EXPECT_TRUE(check_asan_env_expectation(true, L"1"));
	EXPECT_FALSE(check_asan_env_expectation(false, L"1"));
	EXPECT_TRUE(check_asan_env_expectation(false, L"0"));
	EXPECT_FALSE(check_asan_env_expectation(true, L"0"));
	EXPECT_TRUE(check_asan_env_expectation(true, L""));
	EXPECT_TRUE(check_asan_env_expectation(false, L""));

	const HMODULE release_rt = GetModuleHandleW(L"clang_rt.asan_dynamic-x86_64.dll");
	const HMODULE debug_rt = GetModuleHandleW(L"clang_rt.asan_dbg_dynamic-x86_64.dll");
	const bool runtime_loaded = (release_rt != nullptr) || (debug_rt != nullptr);

#if defined(__SANITIZE_ADDRESS__)
	EXPECT_TRUE(runtime_loaded)
		<< "Expected clang_rt.asan_dynamic-x86_64.dll or clang_rt.asan_dbg_dynamic-x86_64.dll in process";
	alignas(8) uint8_t buffer[64]{};
	EXPECT_EQ(__asan_address_is_poisoned(buffer + 16), 0);
	ASAN_POISON_MEMORY_REGION(buffer + 16, 16);
	EXPECT_NE(__asan_address_is_poisoned(buffer + 16), 0);
	EXPECT_EQ(__asan_address_is_poisoned(buffer), 0);
	ASAN_UNPOISON_MEMORY_REGION(buffer + 16, 16);
	EXPECT_EQ(__asan_address_is_poisoned(buffer + 16), 0);
	RecordProperty("ASan", "enabled");
	std::cout << "[          ] Support.AsanActiveWhenEnabled: ASan=enabled\n";
#else
	EXPECT_FALSE(runtime_loaded)
		<< "Unexpected ASan runtime DLL loaded in non-ASan build";
	RecordProperty("ASan", "disabled");
	std::cout << "[          ] Support.AsanActiveWhenEnabled: ASan=disabled\n";
#endif
}

#if defined(__SANITIZE_ADDRESS__)
__declspec(noinline) uint8_t read_byte_unoptimized(const volatile uint8_t* ptr) noexcept
{
	return *ptr;
}

__declspec(noinline) void trigger_asan_poisoned_read()
{
	alignas(8) uint8_t buf[32]{};
	ASAN_POISON_MEMORY_REGION(buf, sizeof(buf));
	const volatile uint8_t sink = read_byte_unoptimized(buf + 8);
	(void)sink;
	ASAN_UNPOISON_MEMORY_REGION(buf, sizeof(buf));
}
#endif

TEST(Support, AsanInstrumentation)
{
#if defined(__SANITIZE_ADDRESS__)
	std::vector<uint8_t> heap_buf(32, 0x5Au);
	EXPECT_EQ(__asan_address_is_poisoned(heap_buf.data()), 0);
	EXPECT_NE(
		__asan_region_is_poisoned(heap_buf.data(), heap_buf.size() + 1u),
		nullptr)
		<< "Expected heap allocation redzone past end of std::vector buffer to be poisoned under ASan";
	EXPECT_DEATH(trigger_asan_poisoned_read(), "AddressSanitizer");
	RecordProperty("ASanRedzone", "verified");
#else
	RecordProperty("ASanRedzone", "skipped-non-asan");
#endif
}

