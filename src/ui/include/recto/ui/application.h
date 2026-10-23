// recto::ui::Application: the process entry point of the toolkit (design spec
// 4.2). One Win32 message loop per process, owned by Application::run.
#pragma once

#include <span>
#include <string>

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
// --self-test-exit could not confirm the first frame reached the screen: the
// window was not visible or the wait for the desktop compositor failed.
inline constexpr int kExitSelfTestPresentation = 4;

// What --self-test-exit observed in the running process after the first paint.
struct SelfTestFacts {
	// The first window is visible, so the compositor has a frame to present.
	bool window_visible{false};
	// The HRESULT DwmFlush returned: the wait for the composition pass that
	// presents the frame just painted.
	long flush_result{-1};
	// The window's DPI awareness context is per-monitor v2.
	bool per_monitor_v2{false};
	// GetACP().
	unsigned code_page{0};
};

// The self-test exit code for `facts`: presentation is checked first, because
// a frame that never reached the screen makes the later checks meaningless.
[[nodiscard]] int self_test_verdict(const SelfTestFacts& facts) noexcept;

// The startup step that failed and the Windows error it reported, so the app
// can tell the person what went wrong instead of exiting silently.
struct StartupFailure {
	// The Windows function that failed, for example L"CreateWindowExW"; null
	// when startup did not fail.
	const wchar_t* operation{nullptr};
	// GetLastError() for Win32 calls, the HRESULT for COM calls.
	unsigned long error{0};
};

// "<operation> failed: <system message> (0x<error>)", or "<operation> failed
// (0x<error>)" when Windows has no message for the code. Carries no product
// name: the app adds its own.
[[nodiscard]] std::wstring describe_startup_failure(const StartupFailure& failure);

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
	// last window is closed. When it returns kExitStartupFailed and `failure`
	// is not null, `*failure` names the step that failed. Under
	// --self-test-exit the window is shown without activation whatever
	// `show_command` says, because a hidden or minimised window never paints
	// and the self-test would wait forever.
	[[nodiscard]] static int run(const ApplicationOptions& options, int show_command, StartupFailure* failure = nullptr);
};

} // namespace recto::ui
