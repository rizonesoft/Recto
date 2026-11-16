# Recto.exe: the Win32 app shell. Mirrors src/app/Recto.vcxproj: C++/WinRT projection headers from the pinned
# Microsoft.Windows.CppWinRT (build/tools.lock.json), app.manifest embedded by the pinned manifest tool.
set(RECTO_WINRT_DIR "${CMAKE_BINARY_DIR}/generated/winrt")
add_custom_command(
    OUTPUT "${RECTO_WINRT_DIR}/winrt/base.h"
    COMMAND "${RECTO_CPPWINRT}" -input "${RECTO_WINDOWS_SDK_VERSION}" -output "${RECTO_WINRT_DIR}"
    DEPENDS "${RECTO_CPPWINRT}"
    COMMENT "C++/WinRT projection of Windows SDK ${RECTO_WINDOWS_SDK_VERSION}"
    VERBATIM)
add_custom_target(recto-winrt-projection DEPENDS "${RECTO_WINRT_DIR}/winrt/base.h")

add_executable(Recto WIN32
    pch.cpp
    main.cpp
    app.manifest)
recto_target(Recto)
add_dependencies(Recto recto-winrt-projection)
target_include_directories(Recto PRIVATE
    "${CMAKE_SOURCE_DIR}/src/services"
    "${CMAKE_SOURCE_DIR}/src/ui/include"
    "${RECTO_WINRT_DIR}")
target_compile_definitions(Recto PRIVATE WINRT_LEAN_AND_MEAN $<$<CONFIG:Debug>:_DEBUG> $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
target_compile_options(Recto PRIVATE /bigobj)
target_link_libraries(Recto PRIVATE recto-ui recto-services
    d3d11 dxgi dcomp d2d1 dwrite windowscodecs dwmapi uxtheme)
target_link_options(Recto PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF /OPT:ICF>)