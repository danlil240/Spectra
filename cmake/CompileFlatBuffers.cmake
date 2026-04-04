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
set(FLATBUFFERS_BUILD_FLATC ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(flatbuffers)

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
        COMMAND flatc
                --cpp
                --gen-mutable
                --gen-object-api
                --scoped-enums
                -o "${SPECTRA_FBS_CPP_OUT}"
                "${FBS_FILE}"
        COMMAND flatc
                --python
                -o "${SPECTRA_FBS_PY_OUT}"
                "${FBS_FILE}"
        DEPENDS ${FBS_FILE} flatc
        COMMENT "FlatBuffers: compiling ${FBS_NAME}.fbs -> C++ & Python"
    )
endforeach()

add_custom_target(spectra_flatbuffers_gen
    DEPENDS ${FLATBUFFERS_GENERATED_HEADERS}
)
