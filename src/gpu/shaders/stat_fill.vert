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

// Vertex attributes: position (x,y) + alpha multiplier
layout(location = 0) in vec2 in_position;
layout(location = 1) in float in_alpha;

layout(location = 0) out float v_alpha;

void main() {
    vec2 pos = in_position + vec2(data_offset_x, data_offset_y);
    gl_Position = projection * view * model * vec4(pos, 0.0, 1.0);
    v_alpha = in_alpha;
}
