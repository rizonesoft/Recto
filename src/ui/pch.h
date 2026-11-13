// Precompiled header for recto-ui. Windows SDK and C++ standard library
// headers only: this module never includes recto-pdf, recto-core or
// recto-services (design spec 4.1).
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <dwmapi.h>

#include <span>
#include <string>
#include <string_view>
