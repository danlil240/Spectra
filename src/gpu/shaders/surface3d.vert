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

// Vertex attributes: position (x,y,z) + normal (nx,ny,nz)
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec3 v_view_dir;

void main() {
    vec3 pos = in_position + vec3(data_offset_x, data_offset_y, 0.0);
    vec4 world_pos = model * vec4(pos, 1.0);
    v_world_pos = world_pos.xyz;

    // Transform normal by model matrix (assuming uniform scale)
    v_normal = normalize(mat3(model) * in_normal);

    // View direction from surface point to camera
    v_view_dir = normalize(camera_pos - world_pos.xyz);

    gl_Position = projection * view * world_pos;
}
