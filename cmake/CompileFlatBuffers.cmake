# CompileFlatBuffers.cmake
# Provides FlatBuffers (pinned FetchContent copy or, with
# SPECTRA_USE_SYSTEM_FLATBUFFERS, the system package) and compiles the .fbs
# schemas to C++ headers in the build tree.
#
# Usage:
#   include(cmake/CompileFlatBuffers.cmake)
#   # Produces: ${FLATBUFFERS_GENERATED_HEADERS}    (list of .h files)
#   #           ${SPECTRA_FLATBUFFERS_INCLUDE_DIRS} (FlatBuffers runtime headers)
#   #           spectra_flatbuffers_gen             (C++ codegen target, built
#   #                                                as a dependency of spectra)
#   #           spectra_flatbuffers_python          (explicit target: regenerates
#   #                                                python/spectra/_fb_generated)
#
# The Python bindings under python/spectra/_fb_generated are committed and are
# the source of truth for the Python package; a normal build never writes into
# the source tree.  Run `cmake --build build --target spectra_flatbuffers_python`
# after changing a schema and commit the result.

set(_spectra_fb_system FALSE)
if(NOT SPECTRA_USE_SYSTEM_FLATBUFFERS STREQUAL "OFF")
    if(SPECTRA_USE_SYSTEM_FLATBUFFERS STREQUAL "ON")
        find_package(FlatBuffers CONFIG REQUIRED)
    else()
        find_package(FlatBuffers CONFIG QUIET)
    endif()
    find_program(SPECTRA_SYSTEM_FLATC flatc)
    if(FlatBuffers_FOUND AND SPECTRA_SYSTEM_FLATC)
        set(_spectra_fb_system TRUE)
    elseif(SPECTRA_USE_SYSTEM_FLATBUFFERS STREQUAL "ON")
        message(FATAL_ERROR "SPECTRA_USE_SYSTEM_FLATBUFFERS=ON but flatc was not found")
    endif()
endif()

if(_spectra_fb_system)
    # Headers and compiler both come from the system so the generated code's
    # FLATBUFFERS_VERSION static_asserts match the runtime headers.
    if(TARGET flatbuffers::flatbuffers)
        get_target_property(SPECTRA_FLATBUFFERS_INCLUDE_DIRS
            flatbuffers::flatbuffers INTERFACE_INCLUDE_DIRECTORIES)
    elseif(TARGET flatbuffers::flatbuffers_shared)
        get_target_property(SPECTRA_FLATBUFFERS_INCLUDE_DIRS
            flatbuffers::flatbuffers_shared INTERFACE_INCLUDE_DIRECTORIES)
    endif()
    if(NOT SPECTRA_FLATBUFFERS_INCLUDE_DIRS)
        find_path(SPECTRA_FLATBUFFERS_INCLUDE_DIRS flatbuffers/flatbuffers.h REQUIRED)
    endif()
    execute_process(COMMAND ${SPECTRA_SYSTEM_FLATC} --version
        OUTPUT_VARIABLE _spectra_flatc_version OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "FlatBuffers: using system ${_spectra_flatc_version} (${SPECTRA_SYSTEM_FLATC})")
    set(_spectra_flatc_cmd "${SPECTRA_SYSTEM_FLATC}")
    set(_spectra_flatc_dep "")
else()

include(FetchContent)

FetchContent_Declare(
    flatbuffers
    GIT_REPOSITORY https://github.com/google/flatbuffers.git
    GIT_TAG        v25.2.10  # 1c514626e83c20fffa8557e75641848e1e15cd5e
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
set(SPECTRA_FLATBUFFERS_INCLUDE_DIRS "${flatbuffers_SOURCE_DIR}/include")

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

endif() # system vs. fetched FlatBuffers

# ─── Schema compilation ──────────────────────────────────────────────────────

set(SPECTRA_FBS_SCHEMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/ipc/schemas")
set(SPECTRA_FBS_CPP_OUT    "${CMAKE_CURRENT_BINARY_DIR}/generated/flatbuffers")
set(SPECTRA_FBS_PY_OUT     "${CMAKE_CURRENT_SOURCE_DIR}/python/spectra/_fb_generated")

file(GLOB SPECTRA_FBS_FILES "${SPECTRA_FBS_SCHEMA_DIR}/*.fbs")
file(MAKE_DIRECTORY "${SPECTRA_FBS_CPP_OUT}")

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
        DEPENDS ${FBS_FILE} ${_spectra_flatc_dep}
        COMMENT "FlatBuffers: compiling ${FBS_NAME}.fbs -> C++"
    )
endforeach()

add_custom_target(spectra_flatbuffers_gen
    DEPENDS ${FLATBUFFERS_GENERATED_HEADERS}
)

# Explicit (not ALL) target: regenerate the committed Python bindings.
set(_spectra_fb_py_cmds)
foreach(FBS_FILE ${SPECTRA_FBS_FILES})
    list(APPEND _spectra_fb_py_cmds
        COMMAND ${_spectra_flatc_cmd} --python -o "${SPECTRA_FBS_PY_OUT}" "${FBS_FILE}")
endforeach()
add_custom_target(spectra_flatbuffers_python
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SPECTRA_FBS_PY_OUT}"
    ${_spectra_fb_py_cmds}
    DEPENDS ${SPECTRA_FBS_FILES} ${_spectra_flatc_dep}
    COMMENT "FlatBuffers: regenerating Python bindings in ${SPECTRA_FBS_PY_OUT}"
    VERBATIM
)
