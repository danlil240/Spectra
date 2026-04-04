// line.wgsl — WGSL port of line.vert + line.frag for the WebGPU backend.
// Renders thick anti-aliased line segments with optional dash patterns.
// Data is read from a storage buffer (one vec2 per point) rather than vertex
// attributes, matching the Vulkan SSBO-based approach.

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

@group(0) @binding(0) var<uniform> frame      : FrameUBO;
@group(0) @binding(1) var<uniform> series_pc  : SeriesPC;

// ── Bind group 1: per-series vertex data (SSBO) ─────────────────────────────

@group(1) @binding(0) var<storage, read> vertex_data : array<vec2<f32>>;

// ── Vertex output / fragment input ──────────────────────────────────────────

struct VertexOutput {
    @builtin(position)   clip_pos          : vec4<f32>,
    @location(0)         v_distance_to_edge: f32,
    @location(1)         v_line_length     : f32,
    @location(2)         v_along_line      : f32,
    @location(3)         v_cumulative_dist : f32,
};

// ── Vertex shader ─────────────────────────────────────────────────────────────

@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32) -> VertexOutput {
    var out: VertexOutput;

    let segment_index = i32(vertex_index) / 6;
    let tri_vert      = i32(vertex_index) % 6;
    let corner_map    = array<i32, 6>(0, 1, 2, 2, 1, 3);
    let corner        = corner_map[tri_vert];

    let data_offset = vec2<f32>(series_pc.data_offset_x, series_pc.data_offset_y);
    let p0 = vertex_data[segment_index]     + data_offset;
    let p1 = vertex_data[segment_index + 1] + data_offset;

    let mvp   = frame.projection * frame.view * frame.model;
    let clip0 = mvp * vec4<f32>(p0, 0.0, 1.0);
    let clip1 = mvp * vec4<f32>(p1, 0.0, 1.0);

    let ndc0 = clip0.xy / clip0.w;
    let ndc1 = clip1.xy / clip1.w;

    let viewport_size = vec2<f32>(frame.viewport_width, frame.viewport_height);
    var screen0 = (ndc0 * 0.5 + vec2<f32>(0.5)) * viewport_size;
    var screen1 = (ndc1 * 0.5 + vec2<f32>(0.5)) * viewport_size;

    let SCREEN_GUARD = 32768.0;
    screen0 = clamp(screen0, vec2<f32>(-SCREEN_GUARD), viewport_size + vec2<f32>(SCREEN_GUARD));
    screen1 = clamp(screen1, vec2<f32>(-SCREEN_GUARD), viewport_size + vec2<f32>(SCREEN_GUARD));

    var dir = screen1 - screen0;
    let seg_length = length(dir);
    if seg_length > 0.0001 {
        dir = dir / seg_length;
    } else {
        dir = vec2<f32>(1.0, 0.0);
    }

    let perp       = vec2<f32>(-dir.y, dir.x);
    let half_width = series_pc.line_width * 0.5 + 1.0;

    let along = select(0.0, 1.0, corner >= 2);
    let side  = select(-1.0, 1.0, (corner & 1) != 0);

    var screen_pos = mix(screen0, screen1, along);
    screen_pos    += perp * side * half_width;

    let ndc = (screen_pos / viewport_size) * 2.0 - vec2<f32>(1.0);

    let w = select(clip0.w, clip1.w, corner >= 2);
    out.clip_pos = vec4<f32>(ndc * w, 0.0, w);

    out.v_distance_to_edge = side * half_width;
    out.v_line_length      = seg_length;
    out.v_along_line       = along * seg_length;

    // Cumulative distance for dash patterns (walk back up to 512 segments).
    var cumul = 0.0;
    if series_pc.line_style != 0u {
        let walk_limit = min(segment_index, 512);
        for (var i = segment_index - 1; i >= segment_index - walk_limit; i--) {
            let a  = vertex_data[i]     + data_offset;
            let b  = vertex_data[i + 1] + data_offset;
            let ca = mvp * vec4<f32>(a, 0.0, 1.0);
            let cb = mvp * vec4<f32>(b, 0.0, 1.0);
            let sa = (ca.xy / ca.w * 0.5 + vec2<f32>(0.5)) * viewport_size;
            let sb = (cb.xy / cb.w * 0.5 + vec2<f32>(0.5)) * viewport_size;
            cumul += length(sb - sa);
        }
    }
    out.v_cumulative_dist = cumul + along * seg_length;

    return out;
}

// ── Fragment shader ───────────────────────────────────────────────────────────

fn dash_sdf(dist_along: f32) -> f32 {
    if series_pc.dash_count <= 0 || series_pc.dash_total <= 0.0 { return -1.0; }

    let t = dist_along % series_pc.dash_total;

    var accum = 0.0;
    for (var i = 0; i < series_pc.dash_count && i < 8; i++) {
        let seg_start = accum;
        accum        += series_pc.dash_pattern[i];
        let seg_end   = accum;

        if t < seg_end {
            let is_gap     = (i % 2) == 1;
            let d_to_start = t - seg_start;
            let d_to_end   = seg_end - t;
            let d_boundary = min(d_to_start, d_to_end);
            return select(d_boundary, -d_boundary, is_gap);
        }
    }
    return -1.0;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let half_w = series_pc.line_width * 0.5;
    let aa     = 1.0;

    let dist       = abs(in.v_distance_to_edge);
    var edge_alpha = smoothstep(half_w + aa, half_w, dist);

    let cap_start = length(vec2<f32>(in.v_along_line, in.v_distance_to_edge));
    let cap_end   = length(vec2<f32>(in.v_along_line - in.v_line_length, in.v_distance_to_edge));

    if in.v_along_line < 0.0 {
        edge_alpha = min(edge_alpha, smoothstep(half_w + aa, half_w, cap_start));
    }
    if in.v_along_line > in.v_line_length {
        edge_alpha = min(edge_alpha, smoothstep(half_w + aa, half_w, cap_end));
    }

    var dash_alpha = 1.0;
    if series_pc.dash_count > 0 && series_pc.dash_total > 0.0 {
        let d = dash_sdf(in.v_cumulative_dist);
        dash_alpha = smoothstep(aa * 0.5, -aa * 0.5, d);
    }

    let alpha = edge_alpha * dash_alpha;
    if alpha < 0.002 { discard; }

    return vec4<f32>(series_pc.color.rgb, series_pc.color.a * series_pc.opacity * alpha);
}
