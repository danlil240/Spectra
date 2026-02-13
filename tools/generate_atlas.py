#!/usr/bin/env python3
"""
Generate an SDF font atlas for Plotix.

Renders ASCII glyphs (32-126) using a system TTF font via Pillow,
computes a signed distance field for each glyph, and packs them
into a texture atlas PNG with accompanying JSON metrics.

The SDF is replicated across R,G,B channels so it works with the
MSDF shader (median(r,g,b) = r when r==g==b).
"""

import json
import math
import os
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# Configuration
ATLAS_FONT_SIZE = 32        # Render size for glyph rasterization
SDF_RANGE = 4               # Pixel range for the SDF
GLYPH_PAD = SDF_RANGE + 1   # Padding around each glyph in the atlas
ATLAS_SIZE = 512             # Atlas texture size (square)

# Find a suitable font
FONT_PATHS = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
]


def find_font():
    for p in FONT_PATHS:
        if os.path.exists(p):
            return p
    # Try system default
    try:
        f = ImageFont.truetype("DejaVuSans", ATLAS_FONT_SIZE)
        return "DejaVuSans"
    except:
        pass
    print("ERROR: No suitable TTF font found", file=sys.stderr)
    sys.exit(1)


def compute_sdf(binary_img, sdf_range):
    """Compute a signed distance field from a binary (0/1) image."""
    w, h = binary_img.size
    pixels = list(binary_img.getdata())
    
    # Convert to 2D array of booleans (True = inside glyph)
    inside = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            inside[y][x] = pixels[y * w + x] > 127

    # Brute-force distance computation (good enough for small glyphs)
    sdf = [[0.0] * w for _ in range(h)]
    search = sdf_range + 1

    for y in range(h):
        for x in range(w):
            is_in = inside[y][x]
            min_dist = float(search)
            
            # Search in a local window
            for dy in range(-search, search + 1):
                ny = y + dy
                if ny < 0 or ny >= h:
                    continue
                for dx in range(-search, search + 1):
                    nx = x + dx
                    if nx < 0 or nx >= w:
                        continue
                    if inside[ny][nx] != is_in:
                        dist = math.sqrt(dx * dx + dy * dy)
                        min_dist = min(min_dist, dist)

            # Signed: positive inside, negative outside
            if is_in:
                sdf[y][x] = min_dist
            else:
                sdf[y][x] = -min_dist

    return sdf


def sdf_to_image(sdf, w, h, sdf_range):
    """Convert SDF values to 0-255 range, mapped so 0.5 = boundary."""
    img = Image.new("L", (w, h))
    pixels = img.load()
    for y in range(h):
        for x in range(w):
            # Map [-sdf_range, +sdf_range] to [0, 1]
            val = (sdf[y][x] / sdf_range) * 0.5 + 0.5
            val = max(0.0, min(1.0, val))
            pixels[x, y] = int(val * 255)
    return img


def main():
    font_path = find_font()
    print(f"Using font: {font_path}")
    
    font = ImageFont.truetype(font_path, ATLAS_FONT_SIZE)
    
    # Measure all ASCII glyphs
    codepoints = list(range(32, 127))  # space (32) through tilde (126)
    glyphs = {}
    
    for cp in codepoints:
        char = chr(cp)
        bbox = font.getbbox(char)
        if bbox is None:
            continue
        
        left, top, right, bottom = bbox
        glyph_w = right - left
        glyph_h = bottom - top
        
        # Get advance width
        advance = font.getlength(char)
        
        glyphs[cp] = {
            "char": char,
            "bbox_left": left,
            "bbox_top": top,
            "width": glyph_w,
            "height": glyph_h,
            "advance": advance,
            "bearing_x": left,
            "bearing_y": -top,  # Distance from baseline to top of glyph
        }
    
    # Pack glyphs into atlas (simple row packing)
    atlas = Image.new("RGB", (ATLAS_SIZE, ATLAS_SIZE), (0, 0, 0))
    
    cursor_x = GLYPH_PAD
    cursor_y = GLYPH_PAD
    row_height = 0
    
    glyph_metrics = []
    
    for cp in sorted(glyphs.keys()):
        g = glyphs[cp]
        char = g["char"]
        
        # Skip space (no visible glyph)
        if cp == 32:
            glyph_metrics.append({
                "unicode": cp,
                "advance": g["advance"] / ATLAS_FONT_SIZE,
            })
            continue
        
        padded_w = int(g["width"]) + GLYPH_PAD * 2
        padded_h = int(g["height"]) + GLYPH_PAD * 2
        
        if padded_w <= 0 or padded_h <= 0:
            continue
        
        # Check if we need to wrap to next row
        if cursor_x + padded_w > ATLAS_SIZE - GLYPH_PAD:
            cursor_x = GLYPH_PAD
            cursor_y += row_height + GLYPH_PAD
            row_height = 0
        
        if cursor_y + padded_h > ATLAS_SIZE - GLYPH_PAD:
            print(f"WARNING: Atlas full at codepoint {cp} ('{char}')", file=sys.stderr)
            break
        
        # Render glyph to a temporary image for SDF computation
        render_w = int(g["width"]) + GLYPH_PAD * 2
        render_h = int(g["height"]) + GLYPH_PAD * 2
        
        tmp = Image.new("L", (render_w, render_h), 0)
        tmp_draw = ImageDraw.Draw(tmp)
        
        # Draw the glyph centered in the padded region
        draw_x = GLYPH_PAD - int(g["bbox_left"])
        draw_y = GLYPH_PAD - int(g["bbox_top"])
        tmp_draw.text((draw_x, draw_y), char, fill=255, font=font)
        
        # Compute SDF
        sdf = compute_sdf(tmp, SDF_RANGE)
        sdf_img = sdf_to_image(sdf, render_w, render_h, SDF_RANGE)
        
        # Convert to RGB (replicate SDF across R,G,B for MSDF shader compatibility)
        sdf_rgb = Image.merge("RGB", [sdf_img, sdf_img, sdf_img])
        
        # Paste into atlas
        atlas.paste(sdf_rgb, (cursor_x, cursor_y))
        
        # Record atlas bounds (in pixels)
        atlas_left = cursor_x
        atlas_top = cursor_y
        atlas_right = cursor_x + render_w
        atlas_bottom = cursor_y + render_h
        
        # Plane bounds (normalized to font size)
        plane_left = g["bearing_x"] / ATLAS_FONT_SIZE
        plane_top = g["bearing_y"] / ATLAS_FONT_SIZE
        plane_right = plane_left + g["width"] / ATLAS_FONT_SIZE
        plane_bottom = plane_top - g["height"] / ATLAS_FONT_SIZE
        
        glyph_metrics.append({
            "unicode": cp,
            "advance": g["advance"] / ATLAS_FONT_SIZE,
            "planeBounds": {
                "left": round(plane_left, 4),
                "bottom": round(plane_bottom, 4),
                "right": round(plane_right, 4),
                "top": round(plane_top, 4),
            },
            "atlasBounds": {
                "left": atlas_left,
                "bottom": atlas_bottom,
                "right": atlas_right,
                "top": atlas_top,
            }
        })
        
        row_height = max(row_height, render_h)
        cursor_x += padded_w + 2  # 2px gap between glyphs
    
    # Get font metrics
    ascent, descent = font.getmetrics()
    line_height = (ascent + descent) / ATLAS_FONT_SIZE
    
    # Build JSON
    metrics_json = {
        "atlas": {
            "type": "msdf",
            "width": ATLAS_SIZE,
            "height": ATLAS_SIZE,
            "size": ATLAS_FONT_SIZE,
            "distanceRange": SDF_RANGE,
            "yOrigin": "top"
        },
        "metrics": {
            "lineHeight": round(line_height, 4),
            "ascender": round(ascent / ATLAS_FONT_SIZE, 4),
            "descender": round(-descent / ATLAS_FONT_SIZE, 4),
            "underlineY": -0.15,
            "underlineThickness": 0.05
        },
        "glyphs": glyph_metrics
    }
    
    # Save outputs
    out_dir = Path(__file__).parent.parent / "src" / "text" / "atlas_data"
    out_dir.mkdir(parents=True, exist_ok=True)
    
    atlas_png = out_dir / "atlas.png"
    atlas_json = out_dir / "atlas.json"
    
    atlas.save(str(atlas_png))
    with open(atlas_json, "w") as f:
        json.dump(metrics_json, f, indent=2)
    
    print(f"Atlas saved to: {atlas_png}")
    print(f"Metrics saved to: {atlas_json}")
    print(f"Glyphs packed: {len(glyph_metrics)}")
    print(f"Atlas size: {ATLAS_SIZE}x{ATLAS_SIZE}")
    
    # Also generate the C++ embedded data
    generate_cpp_header(atlas_png, atlas_json, metrics_json, out_dir)


def generate_cpp_header(atlas_png, atlas_json, metrics_json, out_dir):
    """Generate C++ source file with embedded atlas data."""
    
    # Read PNG bytes
    with open(atlas_png, "rb") as f:
        png_bytes = f.read()
    
    # Read JSON string
    json_str = json.dumps(metrics_json)
    
    # Format as C++ byte array
    lines = []
    for i in range(0, len(png_bytes), 16):
        chunk = png_bytes[i:i+16]
        hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hex_vals},")
    
    png_array = "\n".join(lines)
    
    cpp_content = f'''#include "embedded_font.hpp"

namespace plotix {{
namespace embedded {{

// Auto-generated MSDF font atlas — {len(png_bytes)} bytes PNG
const uint8_t font_atlas_png[] = {{
{png_array}
}};

const size_t font_atlas_png_size = sizeof(font_atlas_png);

const char font_atlas_metrics_json[] = R"({json_str})";

}} // namespace embedded
}} // namespace plotix
'''
    
    # Write to the embedded_font.cpp location
    embedded_cpp = Path(__file__).parent.parent / "src" / "text" / "embedded_font.cpp"
    with open(embedded_cpp, "w") as f:
        f.write(cpp_content)
    
    print(f"Embedded C++ written to: {embedded_cpp}")
    print(f"PNG data size: {len(png_bytes)} bytes")


if __name__ == "__main__":
    main()
