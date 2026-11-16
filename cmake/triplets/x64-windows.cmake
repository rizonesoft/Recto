# Recto's overlay of vcpkg's stock x64-windows triplet (named in vcpkg-configuration.json "overlay-triplets", so the
# CMake and MSBuild builds both take it): the stock settings, and the dependencies built with the MSVC toolset the
# developer prompt selected (VCToolsVersion), the one Recto itself compiles with, never only the newest toolset
# folder. On 2026-11-16 a preview toolset folder holding only ATL and MFC, with no compiler, made vcpkg fall back to
# Clang for gtest, and every local build failed (D00 T04 section 15).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_PROVIDED_FORTRAN ON)
if(DEFINED ENV{VCToolsVersion} AND NOT "$ENV{VCToolsVersion}" STREQUAL "")
    set(VCPKG_PLATFORM_TOOLSET_VERSION "$ENV{VCToolsVersion}")
endif()
