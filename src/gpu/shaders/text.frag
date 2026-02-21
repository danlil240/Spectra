#version 450

// Font atlas texture (set 1, binding 0)
layout(set = 1, binding = 0) uniform sampler2D font_atlas;

layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec4 frag_color;

layout(location = 0) out vec4 out_color;

void main() {
    float raw = texture(font_atlas, frag_uv).r;
    if (raw < 0.01)
        discard;
    // Sharpen alpha edge for crisp text (tight range = sharp edges like ImGui)
    float alpha = smoothstep(0.15, 0.45, raw);
    out_color = vec4(frag_color.rgb, frag_color.a * alpha);
}
