# SpectraDependencyOptions.cmake
#
# Per-dependency switches that choose between a system-installed package and
# the copy Spectra fetches with FetchContent (pinned to a release tag).
#
#   SPECTRA_USE_SYSTEM_<DEP> = AUTO | ON | OFF
#     AUTO  try find_package() first, fall back to the bundled/fetched copy
#     ON    require the system package (configure fails if it is missing)
#     OFF   always use the bundled/fetched copy
#
# For fully offline builds with the bundled copies, point FetchContent at
# local checkouts of the pinned tags, e.g.
#   -DFETCHCONTENT_SOURCE_DIR_GLFW=/src/glfw-3.4
#   -DFETCHCONTENT_FULLY_DISCONNECTED=ON
# (see BUILD_ENVIRONMENT.md, "Dependencies: system vs. bundled").

# spectra_dependency_option(<DEP> <default> <doc>)
function(spectra_dependency_option dep default doc)
    set(_var SPECTRA_USE_SYSTEM_${dep})
    set(${_var} "${default}" CACHE STRING "${doc} (AUTO | ON | OFF)")
    set_property(CACHE ${_var} PROPERTY STRINGS AUTO ON OFF)
    string(TOUPPER "${${_var}}" _val)
    # Accept CMake boolean spellings (TRUE/YES/1/FALSE/NO/0) as well.
    if(_val STREQUAL "AUTO")
        set(_val AUTO)
    elseif(${_var})
        set(_val ON)
    else()
        set(_val OFF)
    endif()
    set(${_var} "${_val}" PARENT_SCOPE)
endfunction()

# Python wheels and self-contained release packages want everything bundled
# and statically linked, so AUTO lookups are skipped for them.
if(SPECTRA_PYTHON_WHEEL)
    set(_spectra_system_default OFF)
else()
    set(_spectra_system_default AUTO)
endif()

spectra_dependency_option(GLFW ${_spectra_system_default}
    "Use a system GLFW >= 3.3 instead of fetching GLFW 3.4")
spectra_dependency_option(SDL3 ${_spectra_system_default}
    "Use a system SDL3 >= 3.2 instead of fetching SDL 3.2.0")
# FlatBuffers generated code must match the runtime headers exactly, and the
# wire format is shared with the Python client, so default to the pinned copy.
spectra_dependency_option(FLATBUFFERS OFF
    "Use system FlatBuffers headers + flatc instead of fetching v25.2.10")
# Unit tests track GoogleTest 1.14 APIs; distro versions vary.
spectra_dependency_option(GTEST OFF
    "Use a system GoogleTest instead of fetching v1.14.0")
spectra_dependency_option(BENCHMARK OFF
    "Use a system Google Benchmark instead of fetching v1.8.3")
