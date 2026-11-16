# recto-fuzz. Mirrors fuzz/recto-fuzz.vcxproj.
add_executable(recto-fuzz
    pch.cpp
    fuzz_smoke.cpp)
recto_target(recto-fuzz)
target_include_directories(recto-fuzz PRIVATE "${CMAKE_SOURCE_DIR}/src/pdf")
target_link_libraries(recto-fuzz PRIVATE recto-pdf)
target_link_options(recto-fuzz PRIVATE /SUBSYSTEM:CONSOLE)
