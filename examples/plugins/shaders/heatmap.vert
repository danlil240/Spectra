#version 450

// Per-frame UBO (set 0, binding 0) — shared with all Spectra pipelines
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4  projection;
    mat4  view;
    mat4  model;
    vec2  viewport_size;
    float time;
    float _pad0;
    vec3  camera_pos;
    float near_plane;
    vec3  light_dir;
    float far_plane;
};

// Per-series push constant — must match SeriesPushConstants
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

// Heatmap data SSBO (set 1, binding 0)
// Layout: [cols, rows, min_val, max_val, value0, value1, ...]
// Values are stored row-major: value[row * cols + col].
layout(std430, set = 1, binding = 0) readonly buffer VertexData {
    float data[];
};

layout(location = 0) out float v_value;   // normalized [0..1]

void main()
{
    // Instanced rendering: one instance per cell.
    // Each instance = 6 vertices (two triangles forming a quad).
    uint cols      = floatBitsToUint(data[0]);
    uint rows      = floatBitsToUint(data[1]);
    float min_val  = data[2];
    float max_val  = data[3];

    uint cell      = gl_InstanceIndex;
    uint row       = cell / cols;
    uint col       = cell % cols;

    // Cell size in data space: total extent is [0..cols] x [0..rows].
    float cx = float(col);
    float cy = float(row);

    // Quad corner from gl_VertexIndex (0..5 → 2 triangles)
    int corner_map[6] = int[6](0, 1, 2, 2, 1, 3);
    int corner = corner_map[gl_VertexIndex % 6];
    float dx = (corner & 1) != 0 ? 1.0 : 0.0;
    float dy = (corner & 2) != 0 ? 1.0 : 0.0;

    vec2 pos = vec2(cx + dx, cy + dy) + vec2(data_offset_x, data_offset_y);
    gl_Position = projection * view * model * vec4(pos, 0.0, 1.0);

    // Normalize the cell value to [0..1] for the fragment shader colormap.
    float raw = data[4 + row * cols + col];
    float range = max_val - min_val;
    v_value = range > 0.0 ? clamp((raw - min_val) / range, 0.0, 1.0) : 0.5;
}
