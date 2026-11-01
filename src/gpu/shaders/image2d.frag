#version 450

// ImageSeries fragment shader: samples the value grid (set 1) and maps it
// through the colormap lookup table (set 2).  CPU mirror of the mapping:
// src/core/image_colormap.hpp (keep in sync).

layout(set = 1, binding = 0) uniform sampler2D values_tex;   // R32F scalars or RGBA8
layout(set = 2, binding = 0) uniform sampler2D lut_tex;      // n x 1 RGBA8

layout(push_constant) uniform SeriesPC {
    vec4  color;
    float line_width;
    float point_size;
    float data_offset_x;
    float data_offset_y;
    uint  line_style;
    uint  marker_type;
    float marker_size;
    float opacity;
    float dash_pattern[8];
    float dash_total;
    int   dash_count;
    float _pad2[2];
};

const uint FLAG_BILINEAR     = 1u;    // interpolate between samples
const uint FLAG_RGBA         = 2u;    // colour grid: no lookup table
const uint FLAG_BANDED       = 4u;    // filled contours: drop values outside the levels
const uint FLAG_NODES        = 8u;    // samples sit on the extent edges (not cell centres)
const uint FLAG_ORIGIN_UPPER = 16u;   // row 0 at the top
const float BAND_EPS = 1e-6;          // image_colormap::BAND_EPS

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main() {
    uint  flags = uint(dash_count);
    ivec2 size  = textureSize(values_tex, 0);
    vec2  uv    = v_uv;
    if ((flags & FLAG_ORIGIN_UPPER) != 0u)
        uv.y = 1.0 - uv.y;

    // Sample space: integer coordinates are sample centres.
    vec2 p = ((flags & FLAG_NODES) != 0u) ? uv * vec2(max(size - 1, ivec2(0)))
                                         : uv * vec2(size) - 0.5;
    vec2  pc      = clamp(p, vec2(0.0), vec2(size - 1));
    ivec2 nearest = clamp(ivec2(floor(pc + 0.5)), ivec2(0), size - 1);

    vec4 s;
    if ((flags & FLAG_BILINEAR) != 0u) {
        ivec2 i0 = ivec2(floor(pc));
        ivec2 i1 = min(i0 + 1, size - 1);
        vec2  f  = pc - vec2(i0);
        vec4  a  = texelFetch(values_tex, i0, 0);
        vec4  b  = texelFetch(values_tex, ivec2(i1.x, i0.y), 0);
        vec4  c  = texelFetch(values_tex, ivec2(i0.x, i1.y), 0);
        vec4  d  = texelFetch(values_tex, i1, 0);
        s = mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
        // A NaN neighbour poisons the blend: fall back to the nearest sample.
        if (isnan(s.r))
            s = texelFetch(values_tex, nearest, 0);
    } else {
        s = texelFetch(values_tex, nearest, 0);
    }

    if ((flags & FLAG_RGBA) != 0u) {
        float a = s.a * color.a;
        if (a <= 0.0)
            discard;
        out_color = vec4(s.rgb, a);
        return;
    }

    float v = s.r;
    if (isnan(v) || isinf(v))
        discard;
    float vmin  = dash_pattern[4];
    float vmax  = dash_pattern[5];
    float range = vmax - vmin;
    float t     = range != 0.0 ? (v - vmin) / range : 0.5;
    if ((flags & FLAG_BANDED) != 0u && (t < -BAND_EPS || t > 1.0 + BAND_EPS))
        discard;
    t = clamp(t, 0.0, 1.0);
    int  n   = textureSize(lut_tex, 0).x;
    int  idx = clamp(int(t * float(n)), 0, n - 1);
    vec4 lut = texelFetch(lut_tex, ivec2(idx, 0), 0);
    out_color = vec4(lut.rgb, lut.a * color.a);
}
