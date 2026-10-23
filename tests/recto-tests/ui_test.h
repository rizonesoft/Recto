// Test registration for the recto-ui suites (D10 T01 section 1).
//
// The TODO plan names every ui test with three parts, for example
// Ui.Project.IncludeBoundary, and filters on them (--gtest_filter=Ui.Project.*).
// GoogleTest's TEST macro cannot spell that (a suite and a test name are single
// identifiers, so TEST(Ui, ProjectX) filters as Ui.ProjectX). RECTO_UI_TEST
// registers the test through ::testing::RegisterTest under the suite "Ui" and
// the test name "<area>.<name>", so the full name is exactly Ui.<area>.<name>
// and --gtest_filter=Ui.<area>.* selects it. Later ui sections use the same
// macro: RECTO_UI_TEST(Dispatcher, CrossThreadPost) { ... }.
//
// A disabled test, RECTO_UI_TEST(Project, DISABLED_Probe), is registered as
// Ui.DISABLED_Project.Probe so GoogleTest sees the marker. `Ui.Project.*` never
// selects it, even with --gtest_also_run_disabled_tests; run it with
// --gtest_filter=Ui.DISABLED_Project.* --gtest_also_run_disabled_tests.
// Release notes leave it out of the test list (a DISABLED_ case is not
// evidence). Each Ui.<area>.<name> must be unique across files: the symbols
// are file-local, so Ui.Project.UniqueTestIds catches a repeat the linker cannot.
#pragma once

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace recto::ui::test {

// Runs one registered body. The body is a plain function, so a test cannot
// depend on another test's leftovers.
class FunctionTest final : public ::testing::Test {
public:
	explicit FunctionTest(void (*body)()) noexcept : body_(body) {}
	void TestBody() override { body_(); }

private:
	void (*body_)();
};

// The test name registered for RECTO_UI_TEST(area, name): "<area>.<name>". GoogleTest
// disables a test whose case name starts with DISABLED_, and the case name here
// starts with the area, so a disabled test moves the marker to the front
// ("DISABLED_<area>.<rest>") and RECTO_UI_TEST(Project, DISABLED_Probe) stays
// disabled like any other test.
inline std::string registered_name(std::string_view area, std::string_view name)
{
	constexpr std::string_view kDisabled = "DISABLED_";
	if (name.starts_with(kDisabled)) {
		return std::string(kDisabled) + std::string(area) + "." + std::string(name.substr(kDisabled.size()));
	}
	return std::string(area) + "." + std::string(name);
}

} // namespace recto::ui::test

#define RECTO_UI_TEST(area, name)                                                                \
	static void recto_ui_body_##area##_##name();                                                 \
	[[maybe_unused]] static const ::testing::TestInfo* const recto_ui_info_##area##_##name =     \
		::testing::RegisterTest(                                                                 \
			"Ui", ::recto::ui::test::registered_name(#area, #name).c_str(), nullptr, nullptr, __FILE__, __LINE__,                         \
			[]() -> ::testing::Test* {                                                           \
				return new ::recto::ui::test::FunctionTest(&recto_ui_body_##area##_##name);      \
			});                                                                                  \
	static void recto_ui_body_##area##_##name()
