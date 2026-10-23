// recto-tests defaults for GoogleTest flags (D10 T01 section 1).
//
// Every TODO checkpoint runs recto-tests.exe with a --gtest_filter. GoogleTest
// exits 0 when a filter matches no test, so a misspelt or stale filter would
// pass a checkpoint with nothing run. This binary fails that run instead, the
// way GoogleTest's own --gtest_fail_if_no_test_selected does, by changing the
// flag's default and nothing else: it is set during static initialization,
// after gtest.dll has initialized the flag (from GTEST_FAIL_IF_NO_TEST_SELECTED
// when that variable is set) and before InitGoogleTest parses the command line
// and any --gtest_flagfile. So the environment variable, the command line and
// a flag file all still decide, exactly as GoogleTest reads them. Listing
// (--gtest_list_tests) is not a run and is unaffected.
// Proven by Ui.Project.EmptySelectionFails.
//
// The flag is data exported from gtest.dll (the vcpkg x64-windows and
// arm64-windows triplets build GoogleTest as a DLL), and data from a DLL must
// be imported, so this file includes GoogleTest as a shared library. A static
// GoogleTest would fail this file's link loudly, never silently.
#define GTEST_LINKED_AS_SHARED_LIBRARY 1
#include <gtest/gtest.h>

#include <windows.h>

GTEST_DECLARE_bool_(fail_if_no_test_selected);

namespace {

// gtest.dll is initialized before this executable's static objects, and main
// (gtest_main) calls InitGoogleTest after them: this is the default, set
// between the two. GoogleTest has already read the environment variable into
// the flag, so it is left alone when the variable is set.
[[maybe_unused]] const bool kDefaulted = [] {
	if (GetEnvironmentVariableW(L"GTEST_FAIL_IF_NO_TEST_SELECTED", nullptr, 0) == 0) {
		GTEST_FLAG_SET(fail_if_no_test_selected, true);
	}
	return true;
}();

} // namespace
