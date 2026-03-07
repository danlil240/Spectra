#version 450

// Image texture (set 1, binding 0)
layout(set = 1, binding = 0) uniform sampler2D image_texture;

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

layout(location = 0) in vec2 frag_uv;

layout(location = 0) out vec4 out_color;

void main() {
    vec4 tex_color = texture(image_texture, frag_uv);
    out_color = vec4(tex_color.rgb, tex_color.a * opacity);
}
