# recto-services: conversion and OCR services. Mirrors src/services/recto-services.vcxproj.
add_library(recto-services STATIC
    pch.cpp
    services.cpp)
recto_target(recto-services)
target_include_directories(recto-services PRIVATE "${CMAKE_SOURCE_DIR}/src/core")
target_link_libraries(recto-services PUBLIC recto-core)
