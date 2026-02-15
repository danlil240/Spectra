#version 450

// Per-series push constant — must match SeriesPushConstants in backend.hpp
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

layout(location = 0) in float v_distance_to_edge;
layout(location = 1) in float v_line_length;
layout(location = 2) in float v_along_line;
layout(location = 3) in float v_cumulative_dist;

layout(location = 0) out vec4 out_color;

// Compute signed distance to the nearest dash edge along the line.
// Returns negative when inside a dash (draw), positive when in a gap.
float dash_sdf(float dist_along) {
    if (dash_count <= 0 || dash_total <= 0.0) return -1.0;

    float t = mod(dist_along, dash_total);

    // Find which segment we're in and compute distance to nearest boundary
    float accum = 0.0;
    for (int i = 0; i < dash_count && i < 8; i++) {
        float seg_start = accum;
        accum += dash_pattern[i];
        float seg_end = accum;

        if (t < seg_end) {
            bool is_gap = (i % 2) == 1;
            // Distance to nearest boundary of this segment
            float d_to_start = t - seg_start;
            float d_to_end   = seg_end - t;
            float d_boundary  = min(d_to_start, d_to_end);
            return is_gap ? d_boundary : -d_boundary;
        }
    }
    return -1.0;
}

void main() {
    float half_w = line_width * 0.5;
    // AA feather width in pixels — 1.0px gives crisp but smooth edges
    float aa = 1.0;

    // ── Edge anti-aliasing ──────────────────────────────────────────────
    float dist = abs(v_distance_to_edge);
    float edge_alpha = smoothstep(half_w + aa, half_w, dist);

    // Round caps at segment endpoints
    float cap_start = length(vec2(v_along_line, v_distance_to_edge));
    float cap_end   = length(vec2(v_along_line - v_line_length, v_distance_to_edge));

    if (v_along_line < 0.0) {
        edge_alpha = min(edge_alpha, smoothstep(half_w + aa, half_w, cap_start));
    }
    if (v_along_line > v_line_length) {
        edge_alpha = min(edge_alpha, smoothstep(half_w + aa, half_w, cap_end));
    }

    // ── Dash pattern with smooth anti-aliased transitions ───────────────
    float dash_alpha = 1.0;
    if (dash_count > 0 && dash_total > 0.0) {
        // Use round-cap dashes: treat each dash as a capsule SDF.
        // The dash_sdf gives signed distance to nearest gap boundary.
        float d = dash_sdf(v_cumulative_dist);
        // Smooth transition over 1.5px for crisp but not jagged edges
        dash_alpha = smoothstep(aa * 0.5, -aa * 0.5, d);
    }

    float alpha = edge_alpha * dash_alpha;
    if (alpha < 0.002) discard;

    out_color = vec4(color.rgb, color.a * opacity * alpha);
}
