// Recto.exe entry point: a plain Win32 desktop host on Recto's own UI toolkit,
// recto-ui (design spec 4.1, D10 T01 section 2). The screens, view models and
// commands arrive with D02.
#include "pch.h"

#include <recto/ui/application.h>

#include <cstdlib>
#include <span>
#include <string>

namespace {

// Startup failed before the window could show: say so, never exit silently.
// The debugger always gets the detail; a person gets a dialog naming the step,
// the Windows error and what to try. The self-test never shows a dialog,
// because a launcher waits on it with nobody to dismiss one.
void report_startup_failure(const recto::ui::StartupFailure& failure, bool interactive)
{
	const std::wstring detail = recto::ui::describe_startup_failure(failure);
	OutputDebugStringW((L"Recto: " + detail + L"\n").c_str());
	if (!interactive) {
		return;
	}
	const std::wstring text = L"Recto could not start.\n\n" + detail +
		L"\n\nTry starting Recto again. If this keeps happening, restart Windows, then reinstall Recto.";
	MessageBoxW(nullptr, text.c_str(), L"Recto", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show_command)
{
	// A single-threaded apartment on the UI thread: OLE drag-and-drop, the
	// clipboard and the shell dialogs require it (D10 T01 section 17), and
	// Microsoft's guidance for shell dialogs adds COINIT_DISABLE_OLE1DDE.
	// The C runtime splits the command line with the Windows quoting rules;
	// __wargv[0] is the program.
	std::span<const wchar_t* const> arguments;
	if (__wargv != nullptr && __argc > 1) {
		arguments = std::span<const wchar_t* const>(__wargv + 1, static_cast<size_t>(__argc - 1));
	}
	const recto::ui::ApplicationOptions options = recto::ui::parse_arguments(arguments);
	const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (FAILED(com)) {
		report_startup_failure({L"CoInitializeEx", static_cast<unsigned long>(com)}, !options.self_test_exit);
		return recto::ui::kExitStartupFailed;
	}
	recto::ui::StartupFailure failure;
	const int exit_code = recto::ui::Application::run(options, show_command, &failure);
	if (exit_code == recto::ui::kExitStartupFailed) {
		report_startup_failure(failure, !options.self_test_exit);
	}
	CoUninitialize();
	return exit_code;
}
