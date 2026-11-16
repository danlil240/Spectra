// line.wgsl — WGSL port of line.vert + line.frag for the WebGPU backend.
// Renders thick anti-aliased polylines with round joins/caps and optional
// dash patterns.  Data is read from a storage buffer (two floats per point, or
// three with a dash phase) rather than vertex attributes, matching the Vulkan
// SSBO-based approach.  See line.vert for the overall scheme: each segment's
// quad is extended past its endpoints and shaded as a capsule; a pixel is
// drawn only by the nearest segment within +-2 segments so overlapping quads
// never blend the same pixel twice.

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
    viewport_origin: vec2<f32>,
    pixel_scale    : f32,
    _pad1          : f32,
};

// poly_first / poly_last alias SeriesPushConstants::_pad2 (see line.vert).
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
    poly_first    : u32,
    poly_last     : u32,
};

@group(0) @binding(0) var<uniform> frame      : FrameUBO;
@group(0) @binding(1) var<uniform> series_pc  : SeriesPC;

// ── Bind group 1: per-series vertex data (SSBO) ─────────────────────────────

@group(1) @binding(0) var<storage, read> vertex_data : array<f32>;

// ── Vertex output / fragment input ──────────────────────────────────────────

struct VertexOutput {
    @builtin(position)                clip_pos    : vec4<f32>,
    // Screen positions of points i-2 .. i+3 (P2 -> P3 is this segment), in
    // logical px with y up from the viewport's bottom edge.
    @location(0) @interpolate(flat)   p01         : vec4<f32>,
    @location(1) @interpolate(flat)   p23         : vec4<f32>,
    @location(2) @interpolate(flat)   p45         : vec4<f32>,
    @location(3) @interpolate(flat)   phase       : f32,
    @location(4) @interpolate(flat)   neighbours  : u32,
};

const PHASE_FLAG   : u32 = 0x80000000u;
const SCREEN_GUARD : f32 = 32768.0;

fn has_phase() -> bool {
    return (series_pc.poly_last & PHASE_FLAG) != 0u;
}

fn load_point(i: i32) -> vec2<f32> {
    let stride = select(2, 3, has_phase());
    return vec2<f32>(vertex_data[i * stride], vertex_data[i * stride + 1]);
}

fn finite_f32(v: f32) -> bool {
    return (bitcast<u32>(v) & 0x7f800000u) != 0x7f800000u;
}

fn to_screen(p: vec2<f32>, mvp: mat4x4<f32>, viewport_size: vec2<f32>) -> vec2<f32> {
    let data_offset = vec2<f32>(series_pc.data_offset_x, series_pc.data_offset_y);
    let clip = mvp * vec4<f32>(p + data_offset, 0.0, 1.0);
    let s    = (clip.xy / clip.w * 0.5 + vec2<f32>(0.5)) * viewport_size;
    return clamp(s, vec2<f32>(-SCREEN_GUARD), viewport_size + vec2<f32>(SCREEN_GUARD));
}

// ── Vertex shader ─────────────────────────────────────────────────────────────

@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32) -> VertexOutput {
    var out: VertexOutput;

    let segment_index = i32(vertex_index) / 6;
    let tri_vert      = i32(vertex_index) % 6;
    let corner_map    = array<i32, 6>(0, 1, 2, 2, 1, 3);
    let corner        = corner_map[tri_vert];

    let mvp           = frame.projection * frame.view * frame.model;
    let viewport_size = vec2<f32>(frame.viewport_width, frame.viewport_height);

    let last_idx  = series_pc.poly_last & ~PHASE_FLAG;
    let connected = last_idx > series_pc.poly_first;

    var pts: array<vec2<f32>, 6>;
    var ok : array<bool, 6>;
    for (var k = 0; k < 6; k++) {
        let idx    = segment_index - 2 + k;
        let own    = (k == 2) || (k == 3);
        let in_rng = own || (connected && idx >= i32(series_pc.poly_first) && idx <= i32(last_idx));
        var p      = vec2<f32>(0.0);
        if in_rng {
            p = load_point(idx);
        }
        ok[k]  = in_rng && finite_f32(p.x) && finite_f32(p.y);
        pts[k] = vec2<f32>(0.0);
        if ok[k] {
            pts[k] = to_screen(p, mvp, viewport_size);
        }
    }

    var nb = 0u;
    if ok[0] && ok[1] { nb = nb | 1u; }
    if ok[1] && ok[2] { nb = nb | 2u; }
    if ok[3] && ok[4] { nb = nb | 8u; }
    if ok[4] && ok[5] { nb = nb | 16u; }

    let s0 = pts[2];
    let s1 = pts[3];

    var dir = s1 - s0;
    let seg_len = length(dir);
    if seg_len > 0.0001 {
        dir = dir / seg_len;
    } else {
        dir = vec2<f32>(1.0, 0.0);
    }
    let perp = vec2<f32>(-dir.y, dir.x);

    let ext   = series_pc.line_width * 0.5 + 1.0;
    let along = select(-1.0, 1.0, corner >= 2);
    let side  = select(-1.0, 1.0, (corner & 1) != 0);

    let base       = select(s0, s1, corner >= 2);
    let screen_pos = base + dir * along * ext + perp * side * ext;
    let ndc        = (screen_pos / viewport_size) * 2.0 - vec2<f32>(1.0);

    if ok[2] && ok[3] {
        out.clip_pos = vec4<f32>(ndc, 0.0, 1.0);
    } else {
        out.clip_pos = vec4<f32>(0.0);
    }

    out.p01        = vec4<f32>(pts[0], pts[1]);
    out.p23        = vec4<f32>(s0, s1);
    out.p45        = vec4<f32>(pts[4], pts[5]);
    out.neighbours = nb;
    out.phase      = 0.0;
    if has_phase() {
        out.phase = vertex_data[segment_index * 3 + 2];
    }

    return out;
}

// ── Fragment shader ───────────────────────────────────────────────────────────

fn seg_dist(q: vec2<f32>, a: vec2<f32>, b: vec2<f32>) -> f32 {
    let ab = b - a;
    let aq = q - a;
    let l2 = dot(ab, ab);
    var t  = 0.0;
    if l2 > 0.0 {
        t = clamp(dot(aq, ab) / l2, 0.0, 1.0);
    }
    return length(aq - ab * t);
}

// Distance along the line from arc position t to the nearest dash core
// (dash shortened by half the width at both ends; round caps fill the rest).
fn dash_core_dist(t: f32, half_w: f32) -> f32 {
    var best = 1e30;
    var acc  = 0.0;
    for (var i = 0; i < series_pc.dash_count && i < 8; i++) {
        let len = series_pc.dash_pattern[i];
        if (i & 1) == 0 {
            let c = acc + len * 0.5;
            let h = max(len * 0.5 - half_w, 0.0);
            var x = t - c;
            x     = x - series_pc.dash_total * floor(x / series_pc.dash_total + 0.5);
            best  = min(best, abs(x) - h);
        }
        acc += len;
    }
    return best;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    var ps = frame.pixel_scale;
    if !(ps > 0.0) {
        ps = 1.0;
    }
    // Framebuffer y grows downwards; WebGPU NDC (and so screen space here)
    // has y up.
    let fb = in.clip_pos.xy / ps - frame.viewport_origin;
    let q  = vec2<f32>(fb.x, frame.viewport_height - fb.y);

    let a = in.p23.xy;
    let b = in.p23.zw;
    let d = seg_dist(q, a, b);

    let nb = in.neighbours;
    if (nb & 1u) != 0u && seg_dist(q, in.p01.xy, in.p01.zw) <= d { discard; }
    if (nb & 2u) != 0u && seg_dist(q, in.p01.zw, a) <= d { discard; }
    if (nb & 8u) != 0u && seg_dist(q, b, in.p45.xy) < d { discard; }
    if (nb & 16u) != 0u && seg_dist(q, in.p45.xy, in.p45.zw) < d { discard; }

    let half_w = series_pc.line_width * 0.5;
    let aa     = 1.0;

    var dist = d;
    if series_pc.dash_count > 0 && series_pc.dash_total > 0.0 {
        let ab  = b - a;
        let len = length(ab);
        var dir = vec2<f32>(1.0, 0.0);
        if len > 0.0001 {
            dir = ab / len;
        }
        let t = in.phase + dot(q - a, dir);
        let s = dash_core_dist(t, half_w);
        dist  = length(vec2<f32>(max(s, 0.0), d));
    }

    let alpha = 1.0 - smoothstep(half_w, half_w + aa, dist);
    if alpha < 0.002 { discard; }

    return vec4<f32>(series_pc.color.rgb, series_pc.color.a * series_pc.opacity * alpha);
}
