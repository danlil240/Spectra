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

    # Skip expensive byte-loop if output already exists and input hasn't changed.
    if(EXISTS "${EMB_OUTPUT}")
        file(TIMESTAMP "${EMB_INPUT}"  _ts_in)
        file(TIMESTAMP "${EMB_OUTPUT}" _ts_out)
        if(NOT _ts_in STRGREATER _ts_out)
            message(STATUS "Embedded asset up-to-date: ${EMB_OUTPUT}")
            return()
        endif()
    endif()

    file(READ "${EMB_INPUT}" _hex HEX)
    string(LENGTH "${_hex}" _hex_len)
    math(EXPR _size "${_hex_len} / 2")

    # Single-pass regex: prefix each hex pair with 0x and append comma.
    # This is O(n) vs the old byte-by-byte while loop which was O(n²)
    # due to string(SUBSTRING) on the full hex string each iteration.
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
    # Remove trailing comma
    string(REGEX REPLACE ",$" "" _bytes "${_bytes}")

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
