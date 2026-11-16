# Recto's overlay of vcpkg's stock arm64-windows triplet: the stock settings, and the dependencies built with the
# MSVC toolset the developer prompt selected (VCToolsVersion), as cmake/triplets/x64-windows.cmake says.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
if(DEFINED ENV{VCToolsVersion} AND NOT "$ENV{VCToolsVersion}" STREQUAL "")
    set(VCPKG_PLATFORM_TOOLSET_VERSION "$ENV{VCToolsVersion}")
endif()
