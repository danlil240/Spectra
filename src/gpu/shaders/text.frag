#version 450

// Font atlas texture (set 1, binding 0)
layout(set = 1, binding = 0) uniform sampler2D font_atlas;

layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec4 frag_color;

layout(location = 0) out vec4 out_color;

void main() {
    // Atlas stores coverage in alpha channel (oversampled rasterization).
    // Use it directly — no SDF tricks needed. The 2x oversampled bitmap
    // already has proper anti-aliased edges from stb_truetype's PackFont.
    float coverage = texture(font_atlas, frag_uv).a;
    if (coverage < 0.005)
        discard;
    out_color = vec4(frag_color.rgb, frag_color.a * coverage);
}
