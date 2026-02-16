# EmbedShaders.cmake
# Script invoked at build time to read .spv files and generate a C++ header
# with constexpr uint32_t arrays for each shader.
#
# Expected variables:
#   SPIRV_DIR      — directory containing .spv files
#   OUTPUT_HEADER  — path to the output .hpp file

file(GLOB SPV_FILES ${SPIRV_DIR}/*.spv)

set(HEADER_CONTENT "#pragma once\n")
string(APPEND HEADER_CONTENT "// Auto-generated — do not edit.\n")
string(APPEND HEADER_CONTENT "// Embedded SPIR-V shader bytecode.\n\n")
string(APPEND HEADER_CONTENT "#include <cstdint>\n")
string(APPEND HEADER_CONTENT "#include <cstddef>\n\n")
string(APPEND HEADER_CONTENT "namespace spectra::shaders {\n\n")

foreach(SPV_FILE ${SPV_FILES})
    get_filename_component(SPV_NAME ${SPV_FILE} NAME)
    # Convert filename to valid C++ identifier: line.vert.spv -> line_vert
    string(REPLACE "." "_" VAR_NAME ${SPV_NAME})
    string(REPLACE "_spv" "" VAR_NAME ${VAR_NAME})

    file(READ ${SPV_FILE} SPV_HEX HEX)
    string(LENGTH "${SPV_HEX}" HEX_LEN)

    # Convert hex pairs to 0xNN bytes
    set(BYTE_LIST "")
    set(BYTE_COUNT 0)
    math(EXPR LAST_INDEX "${HEX_LEN} - 1")
    set(POS 0)
    while(POS LESS HEX_LEN)
        string(SUBSTRING "${SPV_HEX}" ${POS} 2 HEX_BYTE)
        if(BYTE_LIST)
            string(APPEND BYTE_LIST ",")
        endif()
        # Newline every 16 bytes for readability
        math(EXPR MOD "${BYTE_COUNT} % 16")
        if(MOD EQUAL 0)
            string(APPEND BYTE_LIST "\n    ")
        endif()
        string(APPEND BYTE_LIST "0x${HEX_BYTE}")
        math(EXPR POS "${POS} + 2")
        math(EXPR BYTE_COUNT "${BYTE_COUNT} + 1")
    endwhile()

    string(APPEND HEADER_CONTENT "inline constexpr uint8_t ${VAR_NAME}[] = {${BYTE_LIST}\n};\n")
    string(APPEND HEADER_CONTENT "inline constexpr size_t ${VAR_NAME}_size = sizeof(${VAR_NAME});\n\n")
endforeach()

string(APPEND HEADER_CONTENT "} // namespace spectra::shaders\n")

file(WRITE ${OUTPUT_HEADER} "${HEADER_CONTENT}")
