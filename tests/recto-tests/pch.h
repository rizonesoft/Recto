// Precompiled header for recto-tests: Windows SDK, GoogleTest and C++ standard
// library headers only (zero headers from src/ or tests/, zero ASan-only
// headers, and no rpcndr.h-bearing COM headers that define macro `small`).
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef GTEST_LINKED_AS_SHARED_LIBRARY
#define GTEST_LINKED_AS_SHARED_LIBRARY 1
#endif

#include <windows.h>

#include <gtest/gtest.h>
#include <gtest/gtest-spi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <latch>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <numbers>
#include <optional>
#include <regex>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
