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

// Point positions via SSBO (set 1, binding 0)
layout(std430, set = 1, binding = 0) readonly buffer VertexData {
    vec2 points[];
};

layout(location = 0) out vec2 v_uv;

void main() {
    // Instanced rendering: each instance is a point from the SSBO.
    // Each instance has 4 vertices (quad) via gl_VertexIndex 0..3.
    int point_index = gl_InstanceIndex;
    int corner = gl_VertexIndex;

    vec2 center = points[point_index] + data_offset;

    // Project center to clip space, then to screen space
    vec4 clip_center = projection * vec4(center, 0.0, 1.0);
    vec2 ndc_center = clip_center.xy / clip_center.w;
    vec2 screen_center = (ndc_center * 0.5 + 0.5) * viewport_size;

    // Expand quad in screen space: half size + 1px AA margin
    float half_size = point_size * 0.5 + 1.0;

    // Corner offsets: 0=(-1,-1), 1=(1,-1), 2=(-1,1), 3=(1,1)
    vec2 offset = vec2(
        (corner & 1) == 0 ? -1.0 : 1.0,
        (corner & 2) == 0 ? -1.0 : 1.0
    );

    vec2 screen_pos = screen_center + offset * half_size;

    // Convert back to NDC
    vec2 ndc = (screen_pos / viewport_size) * 2.0 - 1.0;

    gl_Position = vec4(ndc * clip_center.w, 0.0, clip_center.w);

    // UV for SDF circle: ranges from -1 to 1 across the quad
    v_uv = offset;
}
