#version 450

// Per-series push constant (reused for grid)
layout(push_constant) uniform SeriesPC {
    vec4 color;
    float line_width;
    float point_size;
    vec2 data_offset;
};

layout(location = 0) out vec4 out_color;

void main() {
    // Solid color output with configurable alpha from push constant
    out_color = color;
}
