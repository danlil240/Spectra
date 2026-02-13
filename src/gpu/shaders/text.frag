#version 450

// Per-series push constant (reused for text)
layout(push_constant) uniform SeriesPC {
    vec4 color;
    float line_width;
    float point_size;
    vec2 data_offset;
};

// MSDF atlas texture (set 1, binding 0)
layout(set = 1, binding = 0) uniform sampler2D u_atlas;

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 out_color;

// Compute median of three values (core of MSDF technique)
float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

void main() {
    // Sample the MSDF atlas
    vec3 msdf = texture(u_atlas, v_uv).rgb;

    // Compute signed distance from median of RGB channels
    float sd = median(msdf.r, msdf.g, msdf.b);

    // Screen-space derivative for resolution-independent smoothstep width
    float screen_px_distance = fwidth(sd);

    // Smoothstep for crisp edges: 0.5 is the glyph boundary in MSDF space
    float alpha = smoothstep(0.5 - screen_px_distance, 0.5 + screen_px_distance, sd);

    if (alpha < 0.001) discard;

    out_color = vec4(color.rgb, color.a * alpha);
}
