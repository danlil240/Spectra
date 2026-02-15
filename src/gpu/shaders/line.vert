#version 450

// Per-frame UBO (set 0, binding 0)
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 projection;
    mat4 view;
    mat4 model;
    vec2 viewport_size;
    float time;
    float _pad0;
    vec3 camera_pos;
    float near_plane;
    vec3 light_dir;
    float far_plane;
};

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

// Vertex data via SSBO (set 1, binding 0)
layout(std430, set = 1, binding = 0) readonly buffer VertexData {
    vec2 points[];
};

layout(location = 0) out float v_distance_to_edge;
layout(location = 1) out float v_line_length;
layout(location = 2) out float v_along_line;
layout(location = 3) out float v_cumulative_dist;

void main() {
    // Each line segment produces 6 vertices (2 triangles = 1 quad, triangle list).
    // gl_VertexIndex: segment_index * 6 + tri_vert (0..5)
    // Triangle 0: corners 0,1,2  Triangle 1: corners 2,1,3
    int segment_index = gl_VertexIndex / 6;
    int tri_vert = gl_VertexIndex % 6;
    int corner_map[6] = int[6](0, 1, 2, 2, 1, 3);
    int corner = corner_map[tri_vert];

    vec2 data_offset = vec2(data_offset_x, data_offset_y);

    // Fetch the two endpoints of this segment
    vec2 p0 = points[segment_index]     + data_offset;
    vec2 p1 = points[segment_index + 1] + data_offset;

    // Project to clip space, then to screen space for width extrusion
    mat4 mvp = projection * view * model;
    vec4 clip0 = mvp * vec4(p0, 0.0, 1.0);
    vec4 clip1 = mvp * vec4(p1, 0.0, 1.0);

    vec2 ndc0 = clip0.xy / clip0.w;
    vec2 ndc1 = clip1.xy / clip1.w;

    vec2 screen0 = (ndc0 * 0.5 + 0.5) * viewport_size;
    vec2 screen1 = (ndc1 * 0.5 + 0.5) * viewport_size;

    // Direction along the segment in screen space
    vec2 dir = screen1 - screen0;
    float seg_length = length(dir);
    dir = seg_length > 0.0001 ? dir / seg_length : vec2(1.0, 0.0);

    // Perpendicular direction (screen space)
    vec2 perp = vec2(-dir.y, dir.x);

    // Extrusion: half line width + 1px AA margin
    float half_width = line_width * 0.5 + 1.0;

    // Determine which endpoint and which side of the quad
    float along = (corner < 2) ? 0.0 : 1.0;
    float side  = ((corner & 1) == 0) ? -1.0 : 1.0;

    vec2 screen_pos = mix(screen0, screen1, along);
    screen_pos += perp * side * half_width;

    // Convert back from screen space to NDC
    vec2 ndc = (screen_pos / viewport_size) * 2.0 - 1.0;

    gl_Position = vec4(ndc * clip0.w, 0.0, (corner < 2) ? clip0.w : clip1.w);

    // Pass distance to edge for AA in fragment shader
    v_distance_to_edge = side * half_width;
    v_line_length = seg_length;
    v_along_line = along * seg_length;

    // Compute cumulative distance along the polyline for dash patterns.
    // Walk backwards through the SSBO to sum actual screen-space segment
    // lengths.  Cap the walk at 512 segments to bound GPU cost — for very
    // long polylines the pattern may drift slightly past that point, which
    // is visually acceptable.
    float cumul = 0.0;
    int walk_limit = min(segment_index, 512);
    for (int i = segment_index - 1; i >= segment_index - walk_limit; --i) {
        vec2 a = points[i]     + data_offset;
        vec2 b = points[i + 1] + data_offset;
        vec4 ca = mvp * vec4(a, 0.0, 1.0);
        vec4 cb = mvp * vec4(b, 0.0, 1.0);
        vec2 sa = (ca.xy / ca.w * 0.5 + 0.5) * viewport_size;
        vec2 sb = (cb.xy / cb.w * 0.5 + 0.5) * viewport_size;
        cumul += length(sb - sa);
    }
    v_cumulative_dist = cumul + along * seg_length;
}
