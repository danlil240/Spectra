#!/usr/bin/env bash
# Regenerate rust/src/generated/spectra_ipc_generated.rs from the IPC schema.
#
# Uses the flatc that CMake builds (build/_deps/flatbuffers-build/flatc,
# v25.2.10, matching the `flatbuffers` crate version) unless FLATC is set.
# CI runs this with --check to fail when the checked-in file is stale.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
flatc="${FLATC:-$root/build/_deps/flatbuffers-build/flatc}"
schema="$root/src/ipc/schemas/spectra_ipc.fbs"
out="$here/src/generated/spectra_ipc_generated.rs"

if [[ ! -x "$flatc" ]]; then
    echo "flatc not found at $flatc (configure the C++ build first, or set FLATC)" >&2
    exit 1
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"$flatc" --rust -o "$tmp" "$schema"

if [[ "${1:-}" == "--check" ]]; then
    if ! diff -u "$out" "$tmp/spectra_ipc_generated.rs"; then
        echo "rust/src/generated is stale: run rust/generate_flatbuffers.sh" >&2
        exit 1
    fi
    echo "generated Rust FlatBuffers code is up to date"
else
    cp "$tmp/spectra_ipc_generated.rs" "$out"
    echo "wrote $out"
fi
