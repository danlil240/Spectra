#version 450

// Per-series push constant
layout(push_constant) uniform SeriesPC {
    vec4 color;
    float line_width;
    float point_size;
    vec2 data_offset;
};

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 out_color;

void main() {
    // SDF circle: v_uv ranges from -1 to 1
    float dist = length(v_uv);

    // Radius is 1.0 in UV space (maps to point_size/2 in screen pixels)
    // AA band: smoothstep over ~1px in UV space
    float aa_width = 2.0 / point_size; // 1px in UV space (approx)
    float alpha = smoothstep(1.0 + aa_width, 1.0 - aa_width, dist);

    if (alpha < 0.001) discard;

    out_color = vec4(color.rgb, color.a * alpha);
}
