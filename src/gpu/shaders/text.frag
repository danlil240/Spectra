#version 450

// Font atlas texture (set 1, binding 0)
layout(set = 1, binding = 0) uniform sampler2D font_atlas;

layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec4 frag_color;

layout(location = 0) out vec4 out_color;

void main() {
    // Atlas stores coverage in alpha channel (oversampled rasterization).
    float coverage = texture(font_atlas, frag_uv).a;
    if (coverage < 0.005)
        discard;

    // Sharpen edges while preserving thin strokes (small tick labels).
    // Low threshold (0.05) keeps fine detail; upper bound (0.55) makes the
    // transition to full opacity happen sooner, giving crisp glyph edges.
    float alpha = smoothstep(0.05, 0.55, coverage);

    out_color = vec4(frag_color.rgb, frag_color.a * alpha);
}
