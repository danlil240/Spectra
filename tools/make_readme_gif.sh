#!/usr/bin/env bash
# Regenerate the animated README clips (docs/images/hero/*.gif) from
# examples/readme_hero.cpp.  Needs a build with SPECTRA_USE_FFMPEG=ON and the
# ffmpeg CLI; gifsicle is used when present to shrink the files.
#
#   tools/make_readme_gif.sh [build-dir]
set -euo pipefail

BUILD="${1:-build}"
OUT="docs/images/hero"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"

for scene in stream surface; do
    "$BUILD/examples/readme_hero" "$scene" "$TMP/$scene.mp4"
    # Two-pass palette keeps the dark theme's gradients clean at 256 colours.
    ffmpeg -loglevel error -y -i "$TMP/$scene.mp4" \
        -vf "fps=15,scale=720:-1:flags=lanczos,palettegen=stats_mode=diff" "$TMP/pal.png"
    ffmpeg -loglevel error -y -i "$TMP/$scene.mp4" -i "$TMP/pal.png" \
        -lavfi "fps=15,scale=720:-1:flags=lanczos[v];[v][1:v]paletteuse=dither=sierra2_4a" \
        "$OUT/$scene.gif"
    if command -v gifsicle >/dev/null; then
        gifsicle -O3 --lossy=80 -b "$OUT/$scene.gif" 2>/dev/null || true
    fi
    echo "$OUT/$scene.gif: $(du -h "$OUT/$scene.gif" | cut -f1)"
done
