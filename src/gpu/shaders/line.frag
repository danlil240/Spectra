#version 450

// 2D thick polyline fragment shader — see line.vert for the overall scheme.
// Coverage is the capsule SDF of this segment (round joins and caps); a pixel
// that a neighbouring segment is nearer to is left to that segment, so
// overlapping quads never blend the same pixel twice.

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

// Per-series push constant — must match SeriesPushConstants in backend.hpp
// (poly_first / poly_last alias SeriesPushConstants::_pad2, see line.vert).
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

layout(location = 0) flat in vec4 v_p01;
layout(location = 1) flat in vec4 v_p23;
layout(location = 2) flat in vec4 v_p45;
layout(location = 3) flat in float v_phase;
layout(location = 4) flat in uint v_neighbours;

layout(location = 0) out vec4 out_color;

// Distance from q to the segment a-b.  `precise` keeps the result
// bit-identical between the draws of neighbouring segments.
float seg_dist(vec2 q, vec2 a, vec2 b)
{
    precise vec2  ab = b - a;
    precise vec2  aq = q - a;
    precise float l2 = dot(ab, ab);
    precise float t  = l2 > 0.0 ? clamp(dot(aq, ab) / l2, 0.0, 1.0) : 0.0;
    precise float d  = length(aq - ab * t);
    return d;
}

// Signed distance along the line from arc position t to the nearest dash
// "core".  Dash lengths in dash_pattern are visual lengths: each dash is
// drawn as a core shortened by half the width at both ends plus round caps,
// so a dash of length <= width is a round dot.  <= 0 inside a core.
float dash_core_dist(float t, float half_w)
{
    float best = 1e30;
    float acc  = 0.0;
    for (int i = 0; i < dash_count && i < 8; ++i)
    {
        float len = dash_pattern[i];
        if ((i & 1) == 0)
        {
            float c = acc + len * 0.5;
            float h = max(len * 0.5 - half_w, 0.0);
            float x = t - c;
            x -= dash_total * floor(x / dash_total + 0.5);
            best = min(best, abs(x) - h);
        }
        acc += len;
    }
    return best;
}

void main()
{
    float ps = pixel_scale > 0.0 ? pixel_scale : 1.0;
    vec2  q  = gl_FragCoord.xy / ps - viewport_origin;

    vec2  a = v_p23.xy;
    vec2  b = v_p23.zw;
    float d = seg_dist(q, a, b);

    // Ownership: the nearest segment draws the pixel, the lower index on a tie.
    uint nb = v_neighbours;
    if ((nb & 1u) != 0u && seg_dist(q, v_p01.xy, v_p01.zw) <= d)
        discard;
    if ((nb & 2u) != 0u && seg_dist(q, v_p01.zw, a) <= d)
        discard;
    if ((nb & 8u) != 0u && seg_dist(q, b, v_p45.xy) < d)
        discard;
    if ((nb & 16u) != 0u && seg_dist(q, v_p45.xy, v_p45.zw) < d)
        discard;

    float half_w = line_width * 0.5;
    // AA feather width in pixels — 1.0px gives crisp but smooth edges
    float aa = 1.0;

    float dist = d;
    if (dash_count > 0 && dash_total > 0.0)
    {
        vec2  ab  = b - a;
        float len = length(ab);
        vec2  dir = len > 0.0001 ? ab / len : vec2(1.0, 0.0);
        float t   = v_phase + dot(q - a, dir);
        float s   = dash_core_dist(t, half_w);
        dist      = length(vec2(max(s, 0.0), d));
    }

    float alpha = smoothstep(half_w + aa, half_w, dist);
    if (alpha < 0.002)
        discard;

    out_color = vec4(color.rgb, color.a * opacity * alpha);
}
