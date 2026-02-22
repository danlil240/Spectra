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

// Per-series push constant — must match SeriesPushConstants
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

// Vertex attributes: position (screen px + NDC depth), UV, packed RGBA color
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in uint in_color;

layout(location = 0) out vec2 frag_uv;
layout(location = 1) out vec4 frag_color;

void main() {
    frag_uv = in_uv;

    // Unpack RGBA from uint32
    frag_color = vec4(
        float((in_color >>  0) & 0xFFu) / 255.0,
        float((in_color >>  8) & 0xFFu) / 255.0,
        float((in_color >> 16) & 0xFFu) / 255.0,
        float((in_color >> 24) & 0xFFu) / 255.0
    );

    // Screen-space ortho: projection maps pixel coords to clip space.
    // XY are screen pixels, Z is NDC depth (0 for 2D text, actual depth for 3D).
    gl_Position = projection * vec4(in_position.xy, 0.0, 1.0);
    gl_Position.z = in_position.z;  // Pass through NDC depth directly
}
