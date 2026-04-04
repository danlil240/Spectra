// grid.wgsl — WGSL port of grid.vert + grid.frag for the WebGPU backend.
// Used for grid lines (LineList topology) and filled overlay shapes (TriangleList).
// Vertex data comes from a vertex buffer attribute (vec2 position).

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

// ── Vertex shader ─────────────────────────────────────────────────────────────

struct VertexOutput {
    @builtin(position) clip_pos: vec4<f32>,
};

@vertex
fn vs_main(@location(0) in_position: vec2<f32>) -> VertexOutput {
    var out: VertexOutput;
    let pos = in_position + vec2<f32>(series_pc.data_offset_x, series_pc.data_offset_y);
    out.clip_pos = frame.projection * frame.view * frame.model * vec4<f32>(pos, 0.0, 1.0);
    return out;
}

// ── Fragment shader ───────────────────────────────────────────────────────────

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    return series_pc.color;
}
