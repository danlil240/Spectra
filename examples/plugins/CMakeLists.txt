# Example plugins — built as shared libraries (.so / .dll / .dylib)

function(add_spectra_plugin name)
    add_library(${name} SHARED ${name}.cpp)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
    )
    target_compile_features(${name} PRIVATE cxx_std_20)
    # Plugins link against spectra for the C ABI registration functions
    target_link_libraries(${name} PRIVATE spectra)
    # Avoid the default 'lib' prefix on Linux for cleaner plugin names
    set_target_properties(${name} PROPERTIES PREFIX "")
endfunction()

add_spectra_plugin(transform_smooth)
add_spectra_plugin(export_csv)
add_spectra_plugin(overlay_crosshair)

# ─── Heatmap series type plugin (requires shader compilation) ─────────────────

set(HEATMAP_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(HEATMAP_SPIRV_DIR  ${CMAKE_CURRENT_BINARY_DIR}/heatmap_spirv)
set(HEATMAP_SPIRV_HDR  ${CMAKE_CURRENT_BINARY_DIR}/heatmap_spirv.hpp)

file(MAKE_DIRECTORY ${HEATMAP_SPIRV_DIR})

# Compile heatmap shaders to SPIR-V
foreach(_stage vert frag)
    add_custom_command(
        OUTPUT ${HEATMAP_SPIRV_DIR}/heatmap.${_stage}.spv
        COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.2
                -o ${HEATMAP_SPIRV_DIR}/heatmap.${_stage}.spv
                ${HEATMAP_SHADER_DIR}/heatmap.${_stage}
        DEPENDS ${HEATMAP_SHADER_DIR}/heatmap.${_stage}
        COMMENT "Compiling heatmap.${_stage}"
        VERBATIM
    )
endforeach()

# Generate embedded SPIR-V header for the heatmap plugin
add_custom_command(
    OUTPUT ${HEATMAP_SPIRV_HDR}
    COMMAND ${CMAKE_COMMAND}
        -DSPIRV_DIR=${HEATMAP_SPIRV_DIR}
        -DOUTPUT_HEADER=${HEATMAP_SPIRV_HDR}
        -DNAMESPACE=heatmap_shaders
        -P ${CMAKE_SOURCE_DIR}/cmake/EmbedShaders.cmake
    DEPENDS ${HEATMAP_SPIRV_DIR}/heatmap.vert.spv
            ${HEATMAP_SPIRV_DIR}/heatmap.frag.spv
    COMMENT "Generating heatmap_spirv.hpp"
    VERBATIM
)

add_custom_target(heatmap_shaders DEPENDS ${HEATMAP_SPIRV_HDR})

add_library(series_heatmap SHARED series_heatmap.cpp)
target_include_directories(series_heatmap PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_CURRENT_BINARY_DIR}    # for heatmap_spirv.hpp
)
target_compile_features(series_heatmap PRIVATE cxx_std_20)
target_link_libraries(series_heatmap PRIVATE spectra)
set_target_properties(series_heatmap PROPERTIES PREFIX "")
add_dependencies(series_heatmap heatmap_shaders)
