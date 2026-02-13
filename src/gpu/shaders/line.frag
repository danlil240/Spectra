#version 450

// Per-series push constant
layout(push_constant) uniform SeriesPC {
    vec4 color;
    float line_width;
    float point_size;
    vec2 data_offset;
};

layout(location = 0) in float v_distance_to_edge;
layout(location = 1) in float v_line_length;
layout(location = 2) in float v_along_line;

layout(location = 0) out vec4 out_color;

void main() {
    float half_width = line_width * 0.5;

    // Anti-aliasing: smoothstep on distance to edge
    // v_distance_to_edge is signed: positive on one side, negative on the other
    float dist = abs(v_distance_to_edge);
    float alpha = smoothstep(half_width + 1.0, half_width, dist);

    // Round caps at polyline endpoints via SDF semicircle
    float cap_dist_start = length(vec2(v_along_line, v_distance_to_edge));
    float cap_dist_end   = length(vec2(v_along_line - v_line_length, v_distance_to_edge));

    if (v_along_line < 0.0) {
        alpha = min(alpha, smoothstep(half_width + 1.0, half_width, cap_dist_start));
    }
    if (v_along_line > v_line_length) {
        alpha = min(alpha, smoothstep(half_width + 1.0, half_width, cap_dist_end));
    }

    if (alpha < 0.001) discard;

    out_color = vec4(color.rgb, color.a * alpha);
}
