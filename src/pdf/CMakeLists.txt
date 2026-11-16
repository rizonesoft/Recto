# recto-pdf: Recto's native PDF engine (D3, D12). Mirrors src/pdf/recto-pdf.vcxproj.
add_library(recto-pdf STATIC
    pch.cpp
    color.cpp
    crypto.cpp
    display_list.cpp
    document.cpp
    error.cpp
    filters.cpp
    flate_compress.cpp
    font.cpp
    font_charstrings.cpp
    font_cid.cpp
    font_encoding.cpp
    function.cpp
    text_font.cpp
    standard14.cpp
    interpreter.cpp
    rasterizer.cpp
    serialize.cpp
    incremental_writer.cpp
    staged_revisions.cpp
    lexer.cpp
    object.cpp
    parser.cpp
    pdf.cpp
    xref.cpp)
recto_target(recto-pdf)
