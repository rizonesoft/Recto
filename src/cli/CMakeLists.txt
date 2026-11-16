# recto-cli. Mirrors src/cli/recto-cli.vcxproj.
add_executable(recto-cli
    pch.cpp
    main.cpp)
recto_target(recto-cli)
target_include_directories(recto-cli PRIVATE "${CMAKE_SOURCE_DIR}/src/core")
target_link_libraries(recto-cli PRIVATE recto-core)
target_link_options(recto-cli PRIVATE /SUBSYSTEM:CONSOLE)
