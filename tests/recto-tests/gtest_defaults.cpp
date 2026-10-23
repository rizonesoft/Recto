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

#include <string_view>

GTEST_DECLARE_bool_(fail_if_no_test_selected);

namespace {

bool chosen_elsewhere()
{
	if (std::wstring_view(GetCommandLineW()).find(L"gtest_fail_if_no_test_selected") != std::wstring_view::npos) {
		return true;
	}
	return GetEnvironmentVariableW(L"GTEST_FAIL_IF_NO_TEST_SELECTED", nullptr, 0) != 0;
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
