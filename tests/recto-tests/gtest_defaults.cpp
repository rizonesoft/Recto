// recto-tests defaults for GoogleTest flags (D10 T01 section 1).
//
// Every TODO checkpoint runs recto-tests.exe with a --gtest_filter. GoogleTest
// exits 0 when a filter matches no test, so a misspelt or stale filter would
// pass a checkpoint with nothing run. This binary fails that run instead, the
// way GoogleTest's own --gtest_fail_if_no_test_selected does. An explicit
// --gtest_fail_if_no_test_selected=... on the command line, or the
// GTEST_FAIL_IF_NO_TEST_SELECTED environment variable, still decides.
// Listing (--gtest_list_tests) is not a run and is unaffected.
// Proven by Ui.Project.EmptySelectionFails.
//
// The flag is data exported from gtest.dll (the vcpkg x64-windows and
// arm64-windows triplets build GoogleTest as a DLL), and data from a DLL must
// be imported, so this file includes GoogleTest as a shared library. A static
// GoogleTest would fail this file's link loudly, never silently.
#define GTEST_LINKED_AS_SHARED_LIBRARY 1
#include <gtest/gtest.h>

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

GTEST_DECLARE_bool_(fail_if_no_test_selected);

namespace {

// True when an argument is the flag itself (`--gtest_fail_if_no_test_selected`
// or `...=<value>`, with GoogleTest's `-`, `--` and `/` prefixes), never when a
// filter or another argument merely contains its name.
bool is_flag_argument(std::wstring_view argument)
{
	constexpr std::wstring_view kName = L"gtest_fail_if_no_test_selected";
	for (const std::wstring_view prefix : {L"--", L"-", L"/"}) {
		if (argument.starts_with(prefix)) {
			const std::wstring_view rest = argument.substr(prefix.size());
			return rest == kName || (rest.starts_with(kName) && rest.substr(kName.size()).starts_with(L"="));
		}
	}
	return false;
}

// Reads the very arguments GoogleTest parsed (InitGoogleTest keeps a copy,
// testing::internal::GetArgvs). Re-splitting the Windows command line would
// not do: CommandLineToArgvW reads some quoting (a run of three quotes)
// differently from the C runtime, so it could see a flag that GoogleTest read
// as part of a filter.
bool chosen_elsewhere()
{
	bool chosen = false;
	const std::vector<std::string> arguments = ::testing::internal::GetArgvs();
	for (size_t i = 1; i < arguments.size(); ++i) {
		chosen = chosen || is_flag_argument(std::wstring(arguments[i].begin(), arguments[i].end()));
	}
	return chosen || GetEnvironmentVariableW(L"GTEST_FAIL_IF_NO_TEST_SELECTED", nullptr, 0) != 0;
}

// The flags are parsed before the program starts its run and read after, so
// the default is applied here, between the two.
class FailIfNoTestSelected final : public ::testing::EmptyTestEventListener {
public:
	void OnTestProgramStart(const ::testing::UnitTest&) override
	{
		if (!chosen_elsewhere()) {
			GTEST_FLAG_SET(fail_if_no_test_selected, true);
		}
	}
};

[[maybe_unused]] const bool kRegistered = [] {
	::testing::UnitTest::GetInstance()->listeners().Append(new FailIfNoTestSelected);
	return true;
}();

} // namespace
