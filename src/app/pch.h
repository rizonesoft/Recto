// Precompiled header for Recto.exe: the Windows SDK, C++ standard library, and
// the C++/WinRT projection for the WinRT APIs Recto calls outside the UI
// (design spec section 1). The UI itself is recto-ui.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <unknwn.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <cstdlib>
#include <span>
#include <string>
