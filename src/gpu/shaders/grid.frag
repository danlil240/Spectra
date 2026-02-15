#version 450

// Per-series push constant (reused for grid) — must match SeriesPushConstants
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

layout(location = 0) out vec4 out_color;

void main() {
    // Solid color output with configurable alpha from push constant
    out_color = color;
}
