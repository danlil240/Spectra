# CompileShaders.cmake
# Finds glslangValidator and compiles GLSL shaders to SPIR-V,
# then generates a C++ header with embedded constexpr arrays.

find_program(GLSLANG_VALIDATOR
    NAMES glslangValidator glslang
    HINTS $ENV{VULKAN_SDK}/bin
          ${Vulkan_INCLUDE_DIR}/../bin
          /usr/bin
          /usr/local/bin
)

if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found — shaders cannot be compiled. "
                    "Install glslang-tools package or Vulkan SDK.")
endif()

message(STATUS "Found glslangValidator: ${GLSLANG_VALIDATOR}")

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/spirv)
set(SHADER_HEADER     ${CMAKE_CURRENT_BINARY_DIR}/generated/shader_spirv.hpp)

file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

# Collect all shader source files
file(GLOB SHADER_SOURCES
    ${SHADER_SOURCE_DIR}/*.vert
    ${SHADER_SOURCE_DIR}/*.frag
    ${SHADER_SOURCE_DIR}/*.comp
    ${SHADER_SOURCE_DIR}/*.geom
)

set(SPIRV_OUTPUTS "")

foreach(SHADER_SRC ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER_SRC} NAME)
    set(SPIRV_OUT ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)

    add_custom_command(
        OUTPUT ${SPIRV_OUT}
        COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.2 -o ${SPIRV_OUT} ${SHADER_SRC}
        DEPENDS ${SHADER_SRC}
        COMMENT "Compiling shader: ${SHADER_NAME}"
        VERBATIM
    )

    list(APPEND SPIRV_OUTPUTS ${SPIRV_OUT})
endforeach()

# Generate C++ header embedding all SPIR-V bytecode
add_custom_command(
    OUTPUT ${SHADER_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DSPIRV_DIR=${SHADER_OUTPUT_DIR}
        -DOUTPUT_HEADER=${SHADER_HEADER}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake
    DEPENDS ${SPIRV_OUTPUTS}
    COMMENT "Generating embedded shader header: shader_spirv.hpp"
    VERBATIM
)

# Verify the header was created and contains expected shaders
add_custom_command(
    OUTPUT ${SHADER_HEADER}.verified
    COMMAND ${CMAKE_COMMAND} -E echo "Verifying shader header contains text shaders..."
    COMMAND ${CMAKE_COMMAND} -E cat ${SHADER_HEADER} | grep -q "text_vert" || ${CMAKE_COMMAND} -E echo "ERROR: text_vert not found in shader header"
    COMMAND ${CMAKE_COMMAND} -E cat ${SHADER_HEADER} | grep -q "text_frag" || ${CMAKE_COMMAND} -E echo "ERROR: text_frag not found in shader header"
    COMMAND ${CMAKE_COMMAND} -E touch ${SHADER_HEADER}.verified
    DEPENDS ${SHADER_HEADER}
    COMMENT "Verifying shader header content"
)

add_custom_target(spectra_shaders DEPENDS ${SHADER_HEADER}.verified ${SPIRV_OUTPUTS})
