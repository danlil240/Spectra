// heatmap.wgsl — WGSL heatmap pipeline (PipelineType::Heatmap) for the WebGPU
// backend.  Same data layout and colormap as the example heatmap plug-in
// shaders (examples/plugins/shaders/heatmap.{vert,frag}).
//
// Storage buffer (group 1, binding 0), as floats:
//   [cols (u32 bits), rows (u32 bits), min_val, max_val, v0, v1, ...]
// Values are row-major: value[row * cols + col].  Cell (col, row) covers
// [col, col + 1] x [row, row + 1] in data space.  Draw with
// draw_instanced(6, cols * rows): one instance per cell, 6 vertices per quad.

// ── Bind group 0: per-frame and per-series uniforms ─────────────────────────

struct FrameUBO {
    projection     : mat4x4<f32>,
    view           : mat4x4<f32>,
    model          : mat4x4<f32>,
    viewport_width : f32,
    viewport_height: f32,
    time           : f32,
    _pad0          : f32,
    camera_pos     : vec3<f32>,
    near_plane     : f32,
    light_dir      : vec3<f32>,
    far_plane      : f32,
};

struct SeriesPC {
    color         : vec4<f32>,
    line_width    : f32,
    point_size    : f32,
    data_offset_x : f32,
    data_offset_y : f32,
    line_style    : u32,
    marker_type   : u32,
    marker_size   : f32,
    opacity       : f32,
    dash_pattern  : array<f32, 8>,
    dash_total    : f32,
    dash_count    : i32,
    _pad2         : vec2<f32>,
};

@group(0) @binding(0) var<uniform> frame     : FrameUBO;
@group(0) @binding(1) var<uniform> series_pc : SeriesPC;

// ── Bind group 1: heatmap header + cell values (SSBO) ───────────────────────

@group(1) @binding(0) var<storage, read> data : array<f32>;

struct VertexOutput {
    @builtin(position)              clip_pos: vec4<f32>,
    @location(0) @interpolate(flat) v_value : f32,   // normalized [0..1]
};

@vertex
fn vs_main(
    @builtin(vertex_index)   vertex_index  : u32,
    @builtin(instance_index) instance_index: u32,
) -> VertexOutput {
    var out: VertexOutput;

    let cols    = max(bitcast<u32>(data[0]), 1u);
    let min_val = data[2];
    let max_val = data[3];

    let row = instance_index / cols;
    let col = instance_index % cols;

    let corner_map = array<i32, 6>(0, 1, 2, 2, 1, 3);
    let corner     = corner_map[vertex_index % 6u];
    let dx         = select(0.0, 1.0, (corner & 1) != 0);
    let dy         = select(0.0, 1.0, (corner & 2) != 0);

    let pos = vec2<f32>(f32(col) + dx, f32(row) + dy)
            + vec2<f32>(series_pc.data_offset_x, series_pc.data_offset_y);
    out.clip_pos = frame.projection * frame.view * frame.model * vec4<f32>(pos, 0.0, 1.0);

    let raw   = data[4u + row * cols + col];
    let range = max_val - min_val;
    out.v_value = select(0.5, clamp((raw - min_val) / range, 0.0, 1.0), range > 0.0);
    return out;
}

// Viridis-inspired colormap (5 control points, linearly interpolated).
fn viridis(t_in: f32) -> vec3<f32> {
    let c0 = vec3<f32>(0.267, 0.004, 0.329);
    let c1 = vec3<f32>(0.282, 0.140, 0.458);
    let c2 = vec3<f32>(0.127, 0.566, 0.551);
    let c3 = vec3<f32>(0.544, 0.774, 0.247);
    let c4 = vec3<f32>(0.993, 0.906, 0.144);

    let t = clamp(t_in, 0.0, 1.0);
    let s = t * 4.0;
    let i = i32(floor(s));
    let f = fract(s);

    if i >= 4 { return c4; }
    if i == 0 { return mix(c0, c1, f); }
    if i == 1 { return mix(c1, c2, f); }
    if i == 2 { return mix(c2, c3, f); }
    return mix(c3, c4, f);
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    return vec4<f32>(viridis(in.v_value), series_pc.opacity);
}
