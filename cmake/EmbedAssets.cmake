# EmbedAssets.cmake — Convert binary files to C arrays at configure time.
#
# Usage:
#   embed_binary_file(
#       INPUT  third_party/Inter-Regular.ttf
#       OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/inter_font_data.hpp
#       VARIABLE InterFont_ttf
#   )
#
# Produces a header with:
#   static const unsigned char InterFont_ttf_data[] = { 0x00, 0x01, ... };
#   static const unsigned int  InterFont_ttf_size = sizeof(InterFont_ttf_data);

function(embed_binary_file)
    cmake_parse_arguments(EMB "" "INPUT;OUTPUT;VARIABLE" "" ${ARGN})

    if(NOT EMB_INPUT OR NOT EMB_OUTPUT OR NOT EMB_VARIABLE)
        message(FATAL_ERROR "embed_binary_file: INPUT, OUTPUT, and VARIABLE are required")
    endif()

    file(READ "${EMB_INPUT}" _hex HEX)
    string(LENGTH "${_hex}" _hex_len)

    # Build comma-separated hex bytes
    set(_bytes "")
    set(_i 0)
    set(_col 0)
    while(_i LESS _hex_len)
        string(SUBSTRING "${_hex}" ${_i} 2 _byte)
        math(EXPR _i "${_i} + 2")

        if(_bytes)
            string(APPEND _bytes ",")
        endif()

        # Line break every 16 bytes
        if(_col EQUAL 16)
            string(APPEND _bytes "\n    ")
            set(_col 0)
        endif()

        string(APPEND _bytes "0x${_byte}")
        math(EXPR _col "${_col} + 1")
    endwhile()

    math(EXPR _size "${_hex_len} / 2")

    file(WRITE "${EMB_OUTPUT}"
        "#pragma once\n"
        "// Auto-generated from ${EMB_INPUT} — do not edit.\n"
        "// Size: ${_size} bytes\n\n"
        "static const unsigned char ${EMB_VARIABLE}_data[] = {\n"
        "    ${_bytes}\n"
        "};\n\n"
        "static const unsigned int ${EMB_VARIABLE}_size = sizeof(${EMB_VARIABLE}_data);\n"
    )

    message(STATUS "Embedded ${EMB_INPUT} -> ${EMB_OUTPUT} (${_size} bytes)")
endfunction()
