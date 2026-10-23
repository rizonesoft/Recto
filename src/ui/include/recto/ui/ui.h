// recto-ui: Recto's own C++23 UI toolkit (design spec 2026-10-22).
// Depends on the Windows SDK only; it never includes recto-pdf, recto-core or
// recto-services headers, and they never include this one (the D13 boundary).
#pragma once

namespace recto::ui {

// Toolkit version. Bumped whenever a public header changes incompatibly.
inline constexpr int kVersion = 1;

// The same constant, defined in the library so a consumer that calls it
// proves the static library links.
[[nodiscard]] int version() noexcept;

// True when the library's own objects were compiled with AddressSanitizer
// (/fsanitize=address, the build's -EnableASAN), so a test can prove the
// toolkit, not only the test binary, is instrumented.
[[nodiscard]] bool built_with_address_sanitizer() noexcept;

} // namespace recto::ui
