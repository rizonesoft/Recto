// Application::run: the Win32 message loop and the first window.
//
// The window is a bootstrap until ui::Window lands (D10 T01 section 4, custom
// frame, Mica Alt, dark mode, minimum size) and the loop drains through the
// ui::Dispatcher wait once it exists (D10 T01 section 3). What this file owns
// and keeps: one loop per process on the calling thread, the exit code, and
// the --self-test-exit verdict.
#include "pch.h"

#include <recto/ui/application.h>

#include <dwmapi.h>

namespace recto::ui {

namespace {

constexpr wchar_t kWindowClass[] = L"Recto.Bootstrap";
constexpr wchar_t kWindowTitle[] = L"Recto";

struct WindowState {
	bool self_test_exit{false};
	bool first_frame_done{false};
	int exit_code{kExitOk};
};

// What the running process proves about its manifest. The XML on disk can be
// right while the linker never embedded it, so the process checks itself.
int self_test_verdict(HWND window) noexcept
{
	if (AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(window), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == FALSE) {
		return kExitSelfTestDpiAwareness;
	}
	if (GetACP() != CP_UTF8) {
		return kExitSelfTestCodePage;
	}
	return kExitOk;
}

LRESULT CALLBACK bootstrap_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (message == WM_NCCREATE) {
		const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
		SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
	}
	auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	switch (message) {
	case WM_PAINT: {
		PAINTSTRUCT paint{};
		const HDC dc = BeginPaint(window, &paint);
		// A system colour, not a hex value: the bootstrap has no tokens yet (D10 T01 section 8).
		FillRect(dc, &paint.rcPaint, GetSysColorBrush(COLOR_WINDOW));
		EndPaint(window, &paint);
		if (state != nullptr && state->self_test_exit && !state->first_frame_done) {
			state->first_frame_done = true;
			// Wait for the desktop compositor to present the frame just painted, so
			// "first frame" means on screen, not merely drawn.
			(void)DwmFlush();
			state->exit_code = self_test_verdict(window);
			PostMessageW(window, WM_CLOSE, 0, 0);
		}
		return 0;
	}
	case WM_DESTROY:
		PostQuitMessage(state != nullptr ? state->exit_code : kExitOk);
		return 0;
	default:
		return DefWindowProcW(window, message, wparam, lparam);
	}
}

} // namespace

int Application::run(const ApplicationOptions& options, int show_command)
{
	// Mouse input arrives as WM_POINTER messages like touch and pen, so the
	// toolkit has one input path (D10 T01 section 12). Failure is not fatal:
	// the window still receives mouse messages.
	(void)EnableMouseInPointer(TRUE);

	const HINSTANCE instance = GetModuleHandleW(nullptr);
	WNDCLASSEXW window_class{};
	window_class.cbSize = sizeof(window_class);
	window_class.style = CS_HREDRAW | CS_VREDRAW;
	window_class.lpfnWndProc = bootstrap_window_proc;
	window_class.hInstance = instance;
	window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	window_class.lpszClassName = kWindowClass;
	if (RegisterClassExW(&window_class) == 0) {
		return kExitStartupFailed;
	}

	WindowState state;
	state.self_test_exit = options.self_test_exit;
	const HWND window = CreateWindowExW(0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, instance, &state);
	if (window == nullptr) {
		UnregisterClassW(kWindowClass, instance);
		return kExitStartupFailed;
	}
	ShowWindow(window, show_command);
	UpdateWindow(window);

	MSG message{};
	int exit_code = kExitOk;
	for (;;) {
		const BOOL got = GetMessageW(&message, nullptr, 0, 0);
		if (got == 0) {
			exit_code = static_cast<int>(message.wParam);
			break;
		}
		if (got == -1) {
			// The loop cannot continue; take the window down so the process exits.
			DestroyWindow(window);
			exit_code = kExitStartupFailed;
			break;
		}
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
	UnregisterClassW(kWindowClass, instance);
	return exit_code;
}

} // namespace recto::ui
