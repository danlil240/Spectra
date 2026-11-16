#version 450

// Per-frame UBO (set 0, binding 0)
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 projection;
    mat4 view;
    mat4 model;
    vec2 viewport_size;
    float time;
    float _pad0;
    vec3 camera_pos;
    float near_plane;
    vec3 light_dir;
    float far_plane;
};

// Per-series push constant — must match SeriesPushConstants in backend.hpp
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

// Point positions via SSBO (set 1, binding 0).  Plain series: one vec4 per
// point (xyz + padding).  Per-point attributes (_pad2[0] > 0.5, see
// ScatterSeries3D::color_values / sizes): two vec4 per point,
// (xyz, marker diameter) and (rgb, 1 = use this colour / 0 = series colour).
// _pad2[0] > 1.5 reads the same layout for the selection outline: the point's
// own diameter plus point_size (the outline growth), in the series colour.
layout(std430, set = 1, binding = 0) readonly buffer VertexData {
    vec4 points[];
};

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec3 v_model_pos;
layout(location = 2) flat out float v_point_size;
layout(location = 3) flat out vec4 v_point_color;

void main() {
    // Instanced rendering: each instance is a point from the SSBO.
    // Each instance has 6 vertices (two triangles) via gl_VertexIndex 0..5.
    int point_index = gl_InstanceIndex;
    int tri_vert = gl_VertexIndex % 6;
    int corner_map[6] = int[6](0, 1, 2, 2, 1, 3);
    int corner = corner_map[tri_vert];

    bool per_point = _pad2[0] > 0.5;
    bool outline = _pad2[0] > 1.5;
    vec4 p0 = points[per_point ? point_index * 2 : point_index];
    float size = per_point ? max(p0.w, 0.0) + (outline ? point_size : 0.0) : point_size;
    v_point_size = size;
    v_point_color = (per_point && !outline) ? points[point_index * 2 + 1] : vec4(0.0);

    vec3 data_offset = vec3(data_offset_x, data_offset_y, 0.0);
    vec3 center = p0.xyz + data_offset;

    // Transform to model space (pass to fragment for box clipping)
    vec4 model_pos = model * vec4(center, 1.0);

    // Project center to clip space, then to screen space
    vec4 clip_center = projection * view * model_pos;
    vec2 ndc_center = clip_center.xy / clip_center.w;
    vec2 screen_center = (ndc_center * 0.5 + 0.5) * viewport_size;

    // Expand quad in screen space: half size + 1px AA margin
    float half_size = size * 0.5 + 1.0;

    // Corner offsets: 0=(-1,-1), 1=(1,-1), 2=(-1,1), 3=(1,1)
    vec2 offset = vec2(
        (corner & 1) == 0 ? -1.0 : 1.0,
        (corner & 2) == 0 ? -1.0 : 1.0
    );

    vec2 screen_pos = screen_center + offset * half_size;

    // Convert back to NDC
    vec2 ndc = (screen_pos / viewport_size) * 2.0 - 1.0;

    // Use the center's depth for all corners of the quad
    gl_Position = vec4(ndc * clip_center.w, clip_center.z, clip_center.w);

    // UV for SDF shapes: ranges from -1 to 1 across the quad
    // Vulkan framebuffer Y points down, so flip so that +v_uv.y is "up" on
    // screen (marker SDFs such as TriangleUp/Star are authored Y-up).
    v_uv = vec2(offset.x, -offset.y);
    v_model_pos = model_pos.xyz;
}
