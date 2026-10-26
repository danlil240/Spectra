# UpdatePackaging.cmake — regenerate version-dependent packaging manifests
# from version.txt (the single source of truth for the project version).
#
# Run in script mode (or via tools/update_packaging.sh / the
# spectra_update_packaging build target):
#
#   cmake [-D<var>=<value>...] -P cmake/UpdatePackaging.cmake
#
# Variables:
#   SPECTRA_SOURCE_DIR    repository root (default: parent of this file's dir)
#   SPECTRA_SOURCE_SHA256 sha256 of the GitHub source tarball vX.Y.Z.tar.gz
#                         (Homebrew formula, AUR PKGBUILD)
#   SPECTRA_WIN64_SHA256  sha256 of the release asset spectra-X.Y.Z-win64.zip
#                         (Scoop manifest)
#   SPECTRA_FETCH_HASHES  ON: download the published tarball / zip for the
#                         current version and compute the hashes
#   SPECTRA_CHECK         ON: do not write anything; fail if a manifest is out
#                         of date with version.txt (used by ctest)
#
# Hash resolution order: explicit -D value > SPECTRA_FETCH_HASHES download >
# the hash already in the manifest when it is for the same version >
# SPECTRA_HASH_PENDING (a release for this version has not been published yet).
#
# Outputs (templates live next to them as *.in):
#   packaging/homebrew/spectra.rb
#   packaging/scoop/spectra.json
#   packaging/aur/PKGBUILD
#   docs/Doxyfile (PROJECT_NUMBER line only)

cmake_minimum_required(VERSION 3.21)

if(NOT SPECTRA_SOURCE_DIR)
    get_filename_component(SPECTRA_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

file(READ "${SPECTRA_SOURCE_DIR}/version.txt" _ver_raw)
string(STRIP "${_ver_raw}" SPECTRA_VERSION)
if(NOT SPECTRA_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "version.txt does not contain X.Y.Z: '${SPECTRA_VERSION}'")
endif()

set(_pending "SPECTRA_HASH_PENDING")
set(_repo_url "https://github.com/danlil240/Spectra")
set(_source_url "${_repo_url}/archive/refs/tags/v${SPECTRA_VERSION}.tar.gz")
set(_win64_url "${_repo_url}/releases/download/v${SPECTRA_VERSION}/spectra-${SPECTRA_VERSION}-win64.zip")

# _spectra_existing_hash(<out> <file> <version-regex> <hash-regex>)
# Returns the hash in <file> if the file is for the current version.
function(_spectra_existing_hash out file ver_re hash_re)
    set(${out} "" PARENT_SCOPE)
    if(NOT EXISTS "${file}")
        return()
    endif()
    file(READ "${file}" _content)
    if(NOT _content MATCHES "${ver_re}")
        return()
    endif()
    if(NOT CMAKE_MATCH_1 STREQUAL SPECTRA_VERSION)
        return()
    endif()
    if(_content MATCHES "${hash_re}")
        set(${out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    endif()
endfunction()

function(_spectra_download_hash out url)
    set(_tmp "${CMAKE_CURRENT_BINARY_DIR}/_spectra_pkg_download.tmp")
    file(DOWNLOAD "${url}" "${_tmp}" STATUS _status TLS_VERIFY ON)
    list(GET _status 0 _code)
    if(NOT _code EQUAL 0)
        message(FATAL_ERROR "Download failed (${_status}): ${url}")
    endif()
    file(SHA256 "${_tmp}" _hash)
    file(REMOVE "${_tmp}")
    set(${out} "${_hash}" PARENT_SCOPE)
endfunction()

set(_brew "${SPECTRA_SOURCE_DIR}/packaging/homebrew/spectra.rb")
set(_scoop "${SPECTRA_SOURCE_DIR}/packaging/scoop/spectra.json")
set(_aur "${SPECTRA_SOURCE_DIR}/packaging/aur/PKGBUILD")

# ── Source tarball hash (Homebrew + AUR) ─────────────────────────────────────
if(NOT SPECTRA_SOURCE_SHA256 AND SPECTRA_FETCH_HASHES)
    _spectra_download_hash(SPECTRA_SOURCE_SHA256 "${_source_url}")
endif()
if(NOT SPECTRA_SOURCE_SHA256)
    _spectra_existing_hash(SPECTRA_SOURCE_SHA256 "${_brew}"
        "/archive/refs/tags/v([0-9.]+)\\.tar\\.gz" "sha256 \"([0-9a-f]+)\"")
endif()
if(NOT SPECTRA_SOURCE_SHA256)
    _spectra_existing_hash(SPECTRA_SOURCE_SHA256 "${_aur}"
        "pkgver=([0-9.]+)" "sha256sums=\\('([0-9a-f]+)'\\)")
endif()
if(NOT SPECTRA_SOURCE_SHA256)
    set(SPECTRA_SOURCE_SHA256 "${_pending}")
endif()

# ── Windows zip hash (Scoop) ─────────────────────────────────────────────────
if(NOT SPECTRA_WIN64_SHA256 AND SPECTRA_FETCH_HASHES)
    _spectra_download_hash(SPECTRA_WIN64_SHA256 "${_win64_url}")
endif()
if(NOT SPECTRA_WIN64_SHA256)
    _spectra_existing_hash(SPECTRA_WIN64_SHA256 "${_scoop}"
        "\"version\": \"([0-9.]+)\"" "\"hash\": \"([0-9a-f]+)\"")
endif()
if(NOT SPECTRA_WIN64_SHA256)
    set(SPECTRA_WIN64_SHA256 "${_pending}")
endif()

foreach(_h SPECTRA_SOURCE_SHA256 SPECTRA_WIN64_SHA256)
    if(NOT ${_h} STREQUAL _pending AND NOT ${_h} MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR "${_h} is not a hex sha256: '${${_h}}'")
    endif()
    string(TOLOWER "${${_h}}" ${_h})
endforeach()

set(_stale "")

# _spectra_emit(<output> <content>)
function(_spectra_emit output content)
    set(_old "")
    if(EXISTS "${output}")
        file(READ "${output}" _old)
    endif()
    if(_old STREQUAL content)
        return()
    endif()
    if(SPECTRA_CHECK)
        set(_stale "${_stale};${output}" PARENT_SCOPE)
    else()
        file(WRITE "${output}" "${content}")
        message(STATUS "Updated ${output}")
    endif()
endfunction()

foreach(_out IN ITEMS "${_brew}" "${_scoop}" "${_aur}")
    file(READ "${_out}.in" _template)
    string(CONFIGURE "${_template}" _content @ONLY)
    _spectra_emit("${_out}" "${_content}")
endforeach()

# docs/Doxyfile: only the PROJECT_NUMBER line is version dependent.
set(_doxyfile "${SPECTRA_SOURCE_DIR}/docs/Doxyfile")
if(EXISTS "${_doxyfile}")
    file(READ "${_doxyfile}" _dox)
    string(REGEX REPLACE "PROJECT_NUMBER([ \t]*)=[^\n]*"
        "PROJECT_NUMBER\\1= \"${SPECTRA_VERSION}\"" _dox_new "${_dox}")
    _spectra_emit("${_doxyfile}" "${_dox_new}")
endif()

if(SPECTRA_CHECK)
    if(_stale)
        string(REPLACE ";" "\n  " _stale_list "${_stale}")
        message(FATAL_ERROR
            "Packaging manifests are out of date with version.txt (${SPECTRA_VERSION}):"
            "${_stale_list}\nRun tools/update_packaging.sh and commit the result.")
    endif()
    message(STATUS "Packaging manifests match version ${SPECTRA_VERSION}")
endif()
if(SPECTRA_SOURCE_SHA256 STREQUAL _pending OR SPECTRA_WIN64_SHA256 STREQUAL _pending)
    message(STATUS "Note: some sha256 values are ${_pending}; the release job fills them "
                   "(tools/update_packaging.sh --fetch) once v${SPECTRA_VERSION} is published.")
endif()
