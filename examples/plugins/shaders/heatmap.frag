#version 450

layout(location = 0) in float v_value;   // normalized [0..1]

layout(location = 0) out vec4 out_color;

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

// Viridis-inspired colormap (5 control points, linearly interpolated).
vec3 viridis(float t)
{
    const vec3 c0 = vec3(0.267, 0.004, 0.329);   // dark purple
    const vec3 c1 = vec3(0.282, 0.140, 0.458);   // blue-purple
    const vec3 c2 = vec3(0.127, 0.566, 0.551);   // teal
    const vec3 c3 = vec3(0.544, 0.774, 0.247);   // yellow-green
    const vec3 c4 = vec3(0.993, 0.906, 0.144);   // yellow

    t = clamp(t, 0.0, 1.0);
    float s = t * 4.0;
    int   i = int(floor(s));
    float f = fract(s);

    if (i >= 4) return c4;
    if (i == 0) return mix(c0, c1, f);
    if (i == 1) return mix(c1, c2, f);
    if (i == 2) return mix(c2, c3, f);
    return mix(c3, c4, f);
}

void main()
{
    vec3 mapped = viridis(v_value);
    out_color = vec4(mapped, opacity);
}
