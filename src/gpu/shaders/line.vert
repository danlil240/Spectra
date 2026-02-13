#version 450

// Per-frame UBO (set 0, binding 0)
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 projection;
    vec2 viewport_size;
    float time;
    float _pad;
};

// Per-series push constant
layout(push_constant) uniform SeriesPC {
    vec4 color;
    float line_width;
    float point_size;
    vec2 data_offset;
};

// Vertex data via SSBO (set 1, binding 0)
layout(std430, set = 1, binding = 0) readonly buffer VertexData {
    vec2 points[];
};

layout(location = 0) out float v_distance_to_edge;
layout(location = 1) out float v_line_length;
layout(location = 2) out float v_along_line;

void main() {
    // Each line segment produces 4 vertices (2 triangles = 1 quad).
    // gl_VertexIndex: segment_index * 4 + corner (0..3)
    int segment_index = gl_VertexIndex / 4;
    int corner = gl_VertexIndex % 4;

    // Fetch the two endpoints of this segment
    vec2 p0 = points[segment_index]     + data_offset;
    vec2 p1 = points[segment_index + 1] + data_offset;

    // Project to clip space, then to screen space for width extrusion
    vec4 clip0 = projection * vec4(p0, 0.0, 1.0);
    vec4 clip1 = projection * vec4(p1, 0.0, 1.0);

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
    // corner 0: p0, -perp (bottom-left)
    // corner 1: p0, +perp (top-left)
    // corner 2: p1, -perp (bottom-right)
    // corner 3: p1, +perp (top-right)
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
}
