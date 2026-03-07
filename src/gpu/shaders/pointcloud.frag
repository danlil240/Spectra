#version 450

layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 out_color;

void main() {
    // Discard transparent fragments
    if (v_color.a < 0.001) discard;

    // Round point: discard fragments outside unit circle
    vec2 coord = gl_PointCoord * 2.0 - 1.0;
    float dist_sq = dot(coord, coord);
    if (dist_sq > 1.0) discard;

    // Soft edge anti-aliasing
    float alpha = v_color.a * smoothstep(1.0, 0.8, dist_sq);
    out_color = vec4(v_color.rgb, alpha);
}
