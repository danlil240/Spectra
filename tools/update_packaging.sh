#!/usr/bin/env bash
# Regenerate version-dependent packaging manifests from version.txt:
#   packaging/homebrew/spectra.rb, packaging/scoop/spectra.json,
#   packaging/aur/PKGBUILD and docs/Doxyfile (PROJECT_NUMBER).
#
# Usage:
#   tools/update_packaging.sh                 # bump versions, keep known hashes
#   tools/update_packaging.sh --fetch         # also download the published
#                                             # v<version> tarball + win64 zip
#                                             # and record their sha256
#   tools/update_packaging.sh --check         # fail if manifests are stale
#   tools/update_packaging.sh --source-sha256 <hex> --win64-sha256 <hex>
#
# The release job runs it with --fetch (or explicit hashes) after the GitHub
# release for the tag has been published.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
args=(-DSPECTRA_SOURCE_DIR="$root")

while [[ $# -gt 0 ]]; do
    case "$1" in
        --fetch) args+=(-DSPECTRA_FETCH_HASHES=ON) ;;
        --check) args+=(-DSPECTRA_CHECK=ON) ;;
        --source-sha256) shift; args+=(-DSPECTRA_SOURCE_SHA256="$1") ;;
        --win64-sha256) shift; args+=(-DSPECTRA_WIN64_SHA256="$1") ;;
        -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

exec cmake "${args[@]}" -P "$root/cmake/UpdatePackaging.cmake"
