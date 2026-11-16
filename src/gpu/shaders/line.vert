#version 450

// 2D thick polyline renderer: one quad per segment, joined by round joins.
//
// Each segment's quad is extended by half_width + AA at both ends and the
// fragment shader shades it as a capsule (rounded rectangle) SDF, so every
// vertex of the polyline gets a round join and the free ends get round caps.
// The quads of neighbouring segments overlap around shared vertices; to keep
// translucent lines from being blended twice there, each pixel is drawn only
// by the nearest segment within a window of +-2 segments (ties go to the lower
// index).  The vertex shader therefore passes the screen positions of the
// points i-2 .. i+3 to the fragment shader as flat varyings.  The fragment
// shader works from gl_FragCoord, which is bit-identical for every draw that
// covers a pixel, so the ownership test is exact (no seams, no double hits).

// Per-frame UBO (set 0, binding 0) — must match FrameUBO in backend.hpp
layout(set = 0, binding = 0) uniform FrameUBO
{
    mat4  projection;
    mat4  view;
    mat4  model;
    vec2  viewport_size;
    float time;
    float _pad0;
    vec3  camera_pos;
    float near_plane;
    vec3  light_dir;
    float far_plane;
    vec2  viewport_origin;
    float pixel_scale;
    float _pad1;
};

// Per-series push constant — must match SeriesPushConstants in backend.hpp.
// The line pipeline reads SeriesPushConstants::_pad2 as two uints (see
// render/line_geometry.hpp): the polyline's first and last point index in
// the buffer, bit 31 of the latter flagging a buffer of (x, y, dash phase).
// last <= first means "isolated segments" (no joins, every segment capped).
layout(push_constant) uniform SeriesPC
{
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
    uint  poly_first;
    uint  poly_last;
};

// Vertex data via SSBO (set 1, binding 0): tightly packed floats, two (x, y)
// or three (x, y, dash phase) per point.
layout(std430, set = 1, binding = 0) readonly buffer VertexData
{
    float pdata[];
};

// Screen positions (logical px from the viewport's top-left corner) of the
// points i-2 .. i+3; P2 -> P3 is this segment.
layout(location = 0) flat out vec4 v_p01;
layout(location = 1) flat out vec4 v_p23;
layout(location = 2) flat out vec4 v_p45;
// Dash phase at P2 in px (0 without a phase buffer).
layout(location = 3) flat out float v_phase;
// Bit k set: neighbour segment k exists (0: i-2, 1: i-1, 3: i+1, 4: i+2).
layout(location = 4) flat out uint v_neighbours;

const uint  PHASE_FLAG   = 0x80000000u;
const float SCREEN_GUARD = 32768.0;

bool has_phase()
{
    return (poly_last & PHASE_FLAG) != 0u;
}

vec2 load_point(int i)
{
    int stride = has_phase() ? 3 : 2;
    return vec2(pdata[i * stride], pdata[i * stride + 1]);
}

// Data point -> screen position.  Must give bit-identical results for the
// same point in every invocation (the fragment ownership test relies on it).
vec2 to_screen(vec2 p, mat4 mvp)
{
    precise vec4 clip = mvp * vec4(p + vec2(data_offset_x, data_offset_y), 0.0, 1.0);
    precise vec2 s    = (clip.xy / clip.w * 0.5 + 0.5) * viewport_size;
    // Guard-band clamp: far off-screen endpoints (deep zoom with sparse data)
    // reach +-millions of px, where adding a few px of extrusion falls below
    // float ULP.  The hardware clipper discards fragments outside anyway.
    return clamp(s, vec2(-SCREEN_GUARD), viewport_size + SCREEN_GUARD);
}

bool finite2(vec2 p)
{
    return !(isnan(p.x) || isnan(p.y) || isinf(p.x) || isinf(p.y));
}

void main()
{
    // Each segment produces 6 vertices (2 triangles = 1 quad, triangle list).
    // gl_VertexIndex: segment_index * 6 + tri_vert (0..5)
    int segment_index = gl_VertexIndex / 6;
    int tri_vert      = gl_VertexIndex % 6;
    int corner_map[6] = int[6](0, 1, 2, 2, 1, 3);
    int corner        = corner_map[tri_vert];

    mat4 mvp = projection * view * model;

    uint last_idx  = poly_last & ~PHASE_FLAG;
    bool connected = last_idx > poly_first;

    // Points i-2 .. i+3; a point outside the polyline or non-finite (NaN gap)
    // is absent.  Isolated segments only see their own two points.
    vec2 pts[6];
    bool ok[6];
    for (int k = 0; k < 6; ++k)
    {
        int  idx    = segment_index - 2 + k;
        bool own    = (k == 2 || k == 3);
        bool in_rng = own || (connected && idx >= int(poly_first) && idx <= int(last_idx));
        vec2 p      = in_rng ? load_point(idx) : vec2(0.0);
        ok[k]       = in_rng && finite2(p);
        pts[k]      = ok[k] ? to_screen(p, mvp) : vec2(0.0);
    }

    uint nb = 0u;
    if (ok[0] && ok[1])
        nb |= 1u;
    if (ok[1] && ok[2])
        nb |= 2u;
    if (ok[3] && ok[4])
        nb |= 8u;
    if (ok[4] && ok[5])
        nb |= 16u;

    vec2 s0 = pts[2];
    vec2 s1 = pts[3];

    vec2  dir     = s1 - s0;
    float seg_len = length(dir);
    dir           = seg_len > 0.0001 ? dir / seg_len : vec2(1.0, 0.0);
    vec2 perp     = vec2(-dir.y, dir.x);

    // Extrusion: half line width + 1px AA margin, sideways and past both
    // endpoints, so the round joins / caps fit inside the quad.
    float ext = line_width * 0.5 + 1.0;

    float along = (corner < 2) ? -1.0 : 1.0;
    float side  = ((corner & 1) == 0) ? -1.0 : 1.0;

    vec2 screen_pos = ((corner < 2) ? s0 : s1) + dir * along * ext + perp * side * ext;
    vec2 ndc        = (screen_pos / viewport_size) * 2.0 - 1.0;

    // A segment touching a NaN point is a gap: collapse its quad.
    bool drawable = ok[2] && ok[3];
    gl_Position   = drawable ? vec4(ndc, 0.0, 1.0) : vec4(0.0);

    v_p01        = vec4(pts[0], pts[1]);
    v_p23        = vec4(s0, s1);
    v_p45        = vec4(pts[4], pts[5]);
    v_neighbours = nb;
    v_phase      = has_phase() ? pdata[segment_index * 3 + 2] : 0.0;
}
