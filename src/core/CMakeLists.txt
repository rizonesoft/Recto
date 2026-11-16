# recto-core: the DocumentActor, RenderPool and tile scheduler runtime. Mirrors src/core/recto-core.vcxproj.
add_library(recto-core STATIC
    pch.cpp
    atomic_write.cpp
    core.cpp
    document_actor.cpp
    render_pool.cpp
    runtime.cpp
    tile_scheduler.cpp)
recto_target(recto-core)
target_include_directories(recto-core PRIVATE "${CMAKE_SOURCE_DIR}/src/pdf")
target_link_libraries(recto-core PUBLIC recto-pdf)
