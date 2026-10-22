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

} // namespace recto::ui
