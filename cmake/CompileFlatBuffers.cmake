# CompileFlatBuffers.cmake
# Fetches FlatBuffers and compiles .fbs schemas to C++ and Python.
#
# Usage:
#   include(cmake/CompileFlatBuffers.cmake)
#   # Produces: ${FLATBUFFERS_GENERATED_HEADERS}  (list of .h files)
#   #           flatbuffers::flatbuffers           (header-only target)

include(FetchContent)

FetchContent_Declare(
    flatbuffers
    GIT_REPOSITORY https://github.com/google/flatbuffers.git
    GIT_TAG        v25.2.10
    GIT_SHALLOW    TRUE
)

set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATHASH OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATLIB ON CACHE BOOL "" FORCE)

# When cross-compiling (e.g. Emscripten), a flatc built with the target
# toolchain cannot run on the build host.  Use a host flatc instead: either
# one given via SPECTRA_HOST_FLATC / found on PATH, or one built from the same
# fetched sources with the host compiler.
if(CMAKE_CROSSCOMPILING)
    set(FLATBUFFERS_BUILD_FLATC OFF CACHE BOOL "" FORCE)
else()
    set(FLATBUFFERS_BUILD_FLATC ON CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable(flatbuffers)

if(CMAKE_CROSSCOMPILING)
    set(SPECTRA_HOST_FLATC "" CACHE FILEPATH "flatc executable that runs on the build host")
    if(NOT SPECTRA_HOST_FLATC)
        find_program(_spectra_path_flatc flatc NO_CMAKE_FIND_ROOT_PATH)
        if(_spectra_path_flatc)
            set(SPECTRA_HOST_FLATC "${_spectra_path_flatc}")
        endif()
    endif()

    if(SPECTRA_HOST_FLATC)
        set(_spectra_flatc_cmd "${SPECTRA_HOST_FLATC}")
        set(_spectra_flatc_dep "")
    else()
        include(ExternalProject)
        set(_spectra_host_flatc_dir "${CMAKE_CURRENT_BINARY_DIR}/host-flatc")
        # Clear the cross toolchain so the host compiler is used.
        ExternalProject_Add(spectra_host_flatc
            SOURCE_DIR       "${flatbuffers_SOURCE_DIR}"
            BINARY_DIR       "${_spectra_host_flatc_dir}"
            CMAKE_ARGS       -DCMAKE_TOOLCHAIN_FILE=
                             -DCMAKE_BUILD_TYPE=Release
                             -DFLATBUFFERS_BUILD_TESTS=OFF
                             -DFLATBUFFERS_BUILD_FLATLIB=OFF
                             -DFLATBUFFERS_BUILD_FLATHASH=OFF
                             -DFLATBUFFERS_INSTALL=OFF
            BUILD_COMMAND    ${CMAKE_COMMAND} --build . --target flatc
            INSTALL_COMMAND  ""
            BUILD_BYPRODUCTS "${_spectra_host_flatc_dir}/flatc"
        )
        set(_spectra_flatc_cmd "${_spectra_host_flatc_dir}/flatc")
        set(_spectra_flatc_dep spectra_host_flatc)
    endif()
    message(STATUS "FlatBuffers: using host flatc ${_spectra_flatc_cmd}")
else()
    set(_spectra_flatc_cmd flatc)
    set(_spectra_flatc_dep flatc)
endif()

# Suppress warnings in third-party FlatBuffers code (e.g. -Wstringop-overflow in GCC 13+)
foreach(_fb_target flatc flatbuffers)
    if(TARGET ${_fb_target})
        target_compile_options(${_fb_target} PRIVATE
            $<$<CXX_COMPILER_ID:GNU>:-Wno-stringop-overflow>
        )
    endif()
endforeach()

# ─── Schema compilation ──────────────────────────────────────────────────────

set(SPECTRA_FBS_SCHEMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/ipc/schemas")
set(SPECTRA_FBS_CPP_OUT    "${CMAKE_CURRENT_BINARY_DIR}/generated/flatbuffers")
set(SPECTRA_FBS_PY_OUT     "${CMAKE_CURRENT_SOURCE_DIR}/python/spectra/_fb_generated")

file(GLOB SPECTRA_FBS_FILES "${SPECTRA_FBS_SCHEMA_DIR}/*.fbs")
file(MAKE_DIRECTORY "${SPECTRA_FBS_CPP_OUT}")
file(MAKE_DIRECTORY "${SPECTRA_FBS_PY_OUT}")

set(FLATBUFFERS_GENERATED_HEADERS "")

foreach(FBS_FILE ${SPECTRA_FBS_FILES})
    get_filename_component(FBS_NAME ${FBS_FILE} NAME_WE)
    set(GEN_HEADER "${SPECTRA_FBS_CPP_OUT}/${FBS_NAME}_generated.h")
    list(APPEND FLATBUFFERS_GENERATED_HEADERS ${GEN_HEADER})

    add_custom_command(
        OUTPUT  ${GEN_HEADER}
        COMMAND ${_spectra_flatc_cmd}
                --cpp
                --gen-mutable
                --gen-object-api
                --scoped-enums
                -o "${SPECTRA_FBS_CPP_OUT}"
                "${FBS_FILE}"
        COMMAND ${_spectra_flatc_cmd}
                --python
                -o "${SPECTRA_FBS_PY_OUT}"
                "${FBS_FILE}"
        DEPENDS ${FBS_FILE} ${_spectra_flatc_dep}
        COMMENT "FlatBuffers: compiling ${FBS_NAME}.fbs -> C++ & Python"
    )
endforeach()

add_custom_target(spectra_flatbuffers_gen
    DEPENDS ${FLATBUFFERS_GENERATED_HEADERS}
)
