// Command-line options of the toolkit. Kept apart from application.cpp so a
// consumer that only parses arguments (recto-tests) links no window code.
#include "pch.h"

#include <recto/ui/application.h>

#include <string_view>

namespace recto::ui {

ApplicationOptions parse_arguments(std::span<const wchar_t* const> arguments) noexcept
{
	constexpr std::wstring_view kSelfTestExit = L"--self-test-exit";
	ApplicationOptions options;
	for (const wchar_t* argument : arguments) {
		if (argument != nullptr && std::wstring_view(argument) == kSelfTestExit) {
			options.self_test_exit = true;
		}
	}
	return options;
}

} // namespace recto::ui
