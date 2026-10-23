// Recto.exe entry point: a plain Win32 desktop host on Recto's own UI toolkit,
// recto-ui (design spec 4.1, D10 T01 section 2). The screens, view models and
// commands arrive with D02.
#include "pch.h"

#include <recto/ui/application.h>

#include <cstdlib>
#include <span>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show_command)
{
	// A single-threaded apartment on the UI thread: OLE drag-and-drop, the
	// clipboard and the shell dialogs require it (D10 T01 section 17), and
	// Microsoft's guidance for shell dialogs adds COINIT_DISABLE_OLE1DDE.
	const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (FAILED(com)) {
		return recto::ui::kExitStartupFailed;
	}
	// The C runtime splits the command line with the Windows quoting rules;
	// __wargv[0] is the program.
	std::span<const wchar_t* const> arguments;
	if (__wargv != nullptr && __argc > 1) {
		arguments = std::span<const wchar_t* const>(__wargv + 1, static_cast<size_t>(__argc - 1));
	}
	const int exit_code = recto::ui::Application::run(recto::ui::parse_arguments(arguments), show_command);
	CoUninitialize();
	return exit_code;
}
