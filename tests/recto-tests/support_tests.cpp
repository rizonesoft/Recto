#include "test_support.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <gtest/gtest.h>

#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
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
			SetCurrentDirectoryW(original_.c_str());
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

	recto::test::TempDir temp;
	{
		ScopedCurrentDir cwd_guard(temp.path());
		const auto root2 = recto::test::repo_root();
		EXPECT_EQ(root1, root2);

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
	}
	EXPECT_FALSE(std::filesystem::exists(single_dir));

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

		const auto extended = recto::test::to_long_path(long_file);
		const DWORD attrs = GetFileAttributesW(extended.c_str());
		ASSERT_NE(attrs, INVALID_FILE_ATTRIBUTES);
		ASSERT_NE(SetFileAttributesW(extended.c_str(), attrs | FILE_ATTRIBUTE_READONLY), 0);
	}
	EXPECT_FALSE(std::filesystem::exists(recto::test::to_long_path(long_file)));
	EXPECT_FALSE(std::filesystem::exists(recto::test::to_long_path(root_path)));
}
