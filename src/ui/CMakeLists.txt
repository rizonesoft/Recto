# recto-ui: the Vellum toolkit until D10 T04 section 1 moves it to libs/vellum. Mirrors src/ui/recto-ui.vcxproj; it
# links no engine library (D13), which the boundary check proves.
add_library(recto-ui STATIC
    application.cpp
    command_line.cpp
    pch.cpp
    ui.cpp)
recto_target(recto-ui)
target_include_directories(recto-ui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
