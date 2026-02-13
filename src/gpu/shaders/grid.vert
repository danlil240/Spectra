#version 450

// Per-frame UBO (set 0, binding 0)
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 projection;
    vec2 viewport_size;
    float time;
    float _pad;
};

// Per-series push constant (reused for grid)
layout(push_constant) uniform SeriesPC {
    vec4 color;
    float line_width;
    float point_size;
    vec2 data_offset;
};

// Grid line endpoints as vertex attributes
layout(location = 0) in vec2 in_position;

void main() {
    vec2 pos = in_position + data_offset;
    gl_Position = projection * vec4(pos, 0.0, 1.0);
}
