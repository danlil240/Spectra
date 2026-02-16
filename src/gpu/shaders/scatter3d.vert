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

// Point positions via SSBO (set 1, binding 0) — 3D points stored as vec4 (xyz + padding)
layout(std430, set = 1, binding = 0) readonly buffer VertexData {
    vec4 points[];
};

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec3 v_model_pos;

void main() {
    // Instanced rendering: each instance is a point from the SSBO.
    // Each instance has 6 vertices (two triangles) via gl_VertexIndex 0..5.
    int point_index = gl_InstanceIndex;
    int tri_vert = gl_VertexIndex % 6;
    int corner_map[6] = int[6](0, 1, 2, 2, 1, 3);
    int corner = corner_map[tri_vert];

    vec3 data_offset = vec3(data_offset_x, data_offset_y, 0.0);
    vec3 center = points[point_index].xyz + data_offset;

    // Transform to model space (pass to fragment for box clipping)
    vec4 model_pos = model * vec4(center, 1.0);

    // Project center to clip space, then to screen space
    vec4 clip_center = projection * view * model_pos;
    vec2 ndc_center = clip_center.xy / clip_center.w;
    vec2 screen_center = (ndc_center * 0.5 + 0.5) * viewport_size;

    // Expand quad in screen space: half size + 1px AA margin
    float half_size = point_size * 0.5 + 1.0;

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
    v_uv = offset;
    v_model_pos = model_pos.xyz;
}
