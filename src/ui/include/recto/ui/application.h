// recto::ui::Application: the process entry point of the toolkit (design spec
// 4.2). One Win32 message loop per process, owned by Application::run.
#pragma once

#include <span>

namespace recto::ui {

// Process exit codes Application::run returns. A self-test failure names what
// the running process found wrong, so a launcher reads the cause from the code.
inline constexpr int kExitOk = 0;
// The first window could not be registered or created.
inline constexpr int kExitStartupFailed = 1;
// --self-test-exit found the process not per-monitor v2 DPI aware: app.manifest
// is missing or was not embedded.
inline constexpr int kExitSelfTestDpiAwareness = 2;
// --self-test-exit found an ANSI code page other than UTF-8: the manifest's
// activeCodePage setting is missing or was not embedded.
inline constexpr int kExitSelfTestCodePage = 3;

// What the command line asked of the toolkit.
struct ApplicationOptions {
	// --self-test-exit: after the first window's first frame is on screen, check
	// what the manifest promises (per-monitor v2 DPI awareness, UTF-8 code page)
	// and close, so an unattended test launches Recto.exe and reads the verdict
	// from the exit code (D10 T01 section 2).
	bool self_test_exit{false};
};

// Reads the arguments after the program name, already split by the C runtime
// (__wargv), so a quoted path that contains a flag's text is one argument and
// never mistaken for the flag. Flags match exactly; anything the toolkit does
// not know is left to the app.
[[nodiscard]] ApplicationOptions parse_arguments(std::span<const wchar_t* const> arguments) noexcept;

class Application {
public:
	// Creates the first window, shows it with `show_command` (the SW_ value
	// wWinMain received), runs the message loop on the calling thread and
	// returns the process exit code (kExit*). Call it on the thread that
	// initialised COM as a single-threaded apartment; it returns only after the
	// last window is closed.
	[[nodiscard]] static int run(const ApplicationOptions& options, int show_command);
};

} // namespace recto::ui
