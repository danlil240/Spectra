#version 450

// Per-frame UBO (set 0, binding 0)
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 projection;
    vec2 viewport_size;
    float time;
    float _pad;
};

// Per-series push constant (reused for text)
layout(push_constant) uniform SeriesPC {
    vec4 color;
    float line_width;
    float point_size;
    vec2 data_offset;
};

// Per-glyph vertex attributes: position + UV
layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec2 v_uv;

void main() {
    vec2 pos = in_position + data_offset;
    gl_Position = projection * vec4(pos, 0.0, 1.0);
    v_uv = in_uv;
}
