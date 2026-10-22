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
#pragma once

#include <gtest/gtest.h>

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

} // namespace recto::ui::test

#define RECTO_UI_TEST(area, name)                                                                \
	static void recto_ui_body_##area##_##name();                                                 \
	[[maybe_unused]] static const ::testing::TestInfo* const recto_ui_info_##area##_##name =     \
		::testing::RegisterTest(                                                                 \
			"Ui", #area "." #name, nullptr, nullptr, __FILE__, __LINE__,                         \
			[]() -> ::testing::Test* {                                                           \
				return new ::recto::ui::test::FunctionTest(&recto_ui_body_##area##_##name);      \
			});                                                                                  \
	static void recto_ui_body_##area##_##name()
