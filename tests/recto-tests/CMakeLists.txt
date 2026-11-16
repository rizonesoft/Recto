# recto-tests: the GoogleTest suite. Mirrors tests/recto-tests/recto-tests.vcxproj; GoogleTest comes from the vcpkg
# manifest (root vcpkg.json) as a DLL, and Recto.exe is built first because the Ui.App tests launch it from beside
# this executable's folder.
find_package(GTest CONFIG REQUIRED)
# GoogleTest's package marks its include folder system (INTERFACE_SYSTEM_INCLUDE_DIRECTORIES), which CMake passes as
# /external:I with /external:W0; MSBuild's vcpkg integration passes it as plain /I under the same /W4 /WX, so the same here.
set_property(TARGET GTest::gtest GTest::gtest_main PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "")
# ui_*_tests.cpp is a wildcard in the vcxproj too; CONFIGURE_DEPENDS re-reads it on every build, as MSBuild does.
file(GLOB recto_ui_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ui_*_tests.cpp")
add_executable(recto-tests
    pch.cpp
    corpus_tests.cpp
    pdf_crypto_tests.cpp
    pdf_display_list_tests.cpp
    pdf_charstring_tests.cpp
    pdf_cid_tests.cpp
    pdf_font_tests.cpp
    pdf_interpreter_tests.cpp
    actor_tests.cpp
    scheduler_tests.cpp
    runtime_tests.cpp
    pdf_rasterizer_tests.cpp
    pdf_serialize_tests.cpp
    pdf_incremental_writer_tests.cpp
    pdf_staged_revisions_tests.cpp
    pdf_document_tests.cpp
    pdf_error_tests.cpp
    pdf_filters_tests.cpp
    pdf_lexer_tests.cpp
    pdf_object_tests.cpp
    pdf_parser_tests.cpp
    pdf_xref_tests.cpp
    scaffold_smoke.cpp
    support_tests.cpp
    test_support.cpp
    gtest_defaults.cpp
    heap_meter.cpp
    ui_render_harness.cpp
    ${recto_ui_tests}
    recto-tests.manifest)
recto_target(recto-tests)
# GoogleTest's headers as plain /I, as MSBuild's vcpkg integration passes them, held to the same /W4 /WX.
set_target_properties(recto-tests PROPERTIES NO_SYSTEM_FROM_IMPORTED ON)
add_dependencies(recto-tests Recto)
target_include_directories(recto-tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/pdf"
    "${CMAKE_SOURCE_DIR}/src/core"
    "${CMAKE_SOURCE_DIR}/src/services"
    "${CMAKE_SOURCE_DIR}/src/ui/include")
target_compile_definitions(recto-tests PRIVATE GTEST_LINKED_AS_SHARED_LIBRARY=1)
target_link_libraries(recto-tests PRIVATE recto-pdf recto-core recto-services recto-ui
    bcrypt d2d1 windowscodecs ole32 xmllite GTest::gtest_main GTest::gtest)
target_link_options(recto-tests PRIVATE /SUBSYSTEM:CONSOLE)