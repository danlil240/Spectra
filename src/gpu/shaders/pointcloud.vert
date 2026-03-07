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

// Per-point data from SSBO: {x, y, z, packed_rgba}
struct PointData {
    float x, y, z;
    uint  color_packed;
};

layout(std430, set = 0, binding = 1) readonly buffer PointBuffer {
    PointData points[];
};

layout(location = 0) out vec4 v_color;

void main() {
    PointData pt = points[gl_VertexIndex];
    vec3 pos = vec3(pt.x, pt.y, pt.z) + vec3(data_offset_x, data_offset_y, 0.0);
    vec4 world_pos = model * vec4(pos, 1.0);
    gl_Position = projection * view * world_pos;
    gl_PointSize = point_size;

    // Unpack RGBA from uint32: 0xAABBGGRR
    float r = float((pt.color_packed >>  0) & 0xFF) / 255.0;
    float g = float((pt.color_packed >>  8) & 0xFF) / 255.0;
    float b = float((pt.color_packed >> 16) & 0xFF) / 255.0;
    float a = float((pt.color_packed >> 24) & 0xFF) / 255.0;

    // If marker_type != 0, use per-point color; otherwise use push constant color
    if (marker_type != 0u) {
        v_color = vec4(r, g, b, a * opacity);
    } else {
        v_color = vec4(color.rgb, color.a * opacity);
    }
}
