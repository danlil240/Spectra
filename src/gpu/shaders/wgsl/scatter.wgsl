// scatter.wgsl — WGSL port of scatter.vert + scatter.frag for the WebGPU backend.
// Renders instanced SDF scatter markers.  Each instance is one data point read
// from a storage buffer; 6 vertices per instance form a screen-space quad.

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

// ── Bind group 1: per-series vertex data (SSBO) ─────────────────────────────

@group(1) @binding(0) var<storage, read> vertex_data : array<vec2<f32>>;

// ── Vertex output / fragment input ──────────────────────────────────────────

struct VertexOutput {
    @builtin(position) clip_pos: vec4<f32>,
    @location(0)       v_uv    : vec2<f32>,
};

// ── Vertex shader ─────────────────────────────────────────────────────────────

@vertex
fn vs_main(
    @builtin(vertex_index)   vertex_index  : u32,
    @builtin(instance_index) instance_index: u32,
) -> VertexOutput {
    var out: VertexOutput;

    let tri_vert   = i32(vertex_index) % 6;
    let corner_map = array<i32, 6>(0, 1, 2, 2, 1, 3);
    let corner     = corner_map[tri_vert];

    let data_offset  = vec2<f32>(series_pc.data_offset_x, series_pc.data_offset_y);
    let center       = vertex_data[instance_index] + data_offset;

    let clip_center  = frame.projection * frame.view * frame.model * vec4<f32>(center, 0.0, 1.0);
    let ndc_center   = clip_center.xy / clip_center.w;
    let viewport_size = vec2<f32>(frame.viewport_width, frame.viewport_height);
    let screen_center = (ndc_center * 0.5 + vec2<f32>(0.5)) * viewport_size;

    let half_size = series_pc.point_size * 0.5 + 1.0;

    let offset = vec2<f32>(
        select(-1.0, 1.0, (corner & 1) != 0),
        select(-1.0, 1.0, (corner & 2) != 0),
    );

    let screen_pos = screen_center + offset * half_size;
    let ndc        = (screen_pos / viewport_size) * 2.0 - vec2<f32>(1.0);

    out.clip_pos = vec4<f32>(ndc * clip_center.w, 0.0, clip_center.w);
    out.v_uv     = offset;
    return out;
}

// ── SDF helpers ───────────────────────────────────────────────────────────────

fn sdf_circle(p: vec2<f32>) -> f32 {
    return length(p) - 0.85;
}

fn sdf_square(p: vec2<f32>) -> f32 {
    let d = abs(p) - vec2<f32>(0.7);
    return length(max(d, vec2<f32>(0.0))) + min(max(d.x, d.y), 0.0);
}

fn sdf_diamond(p: vec2<f32>) -> f32 {
    let d = abs(p);
    return (d.x + d.y) * 0.7071 - 0.6;
}

fn sdf_triangle_up(p_in: vec2<f32>) -> f32 {
    var p = p_in;
    p.y  += 0.15;
    let k = sqrt(3.0);
    p.x   = abs(p.x) - 0.75;
    p.y   = p.y + 0.75 / k;
    if p.x + k * p.y > 0.0 {
        p = vec2<f32>(p.x - k * p.y, -k * p.x - p.y) / 2.0;
    }
    p.x -= clamp(p.x, -1.5, 0.0);
    return -length(p) * sign(p.y);
}

fn sdf_triangle_down(p: vec2<f32>) -> f32 { return sdf_triangle_up(vec2<f32>(p.x, -p.y)); }
fn sdf_triangle_left(p: vec2<f32>) -> f32 { return sdf_triangle_up(vec2<f32>(p.y, -p.x)); }
fn sdf_triangle_right(p: vec2<f32>) -> f32 { return sdf_triangle_up(vec2<f32>(-p.y, p.x)); }

fn sdf_plus(p: vec2<f32>, arm_w: f32) -> f32 {
    let d = abs(p);
    return min(max(d.x - arm_w, d.y - 0.75), max(d.x - 0.75, d.y - arm_w));
}

fn sdf_cross(p: vec2<f32>, arm_w: f32) -> f32 {
    let r = vec2<f32>(p.x - p.y, p.x + p.y) * 0.7071;
    return sdf_plus(r, arm_w);
}

fn sdf_star(p_in: vec2<f32>) -> f32 {
    let PI  = 3.14159265;
    let an  = PI * 2.0 / 5.0;
    let en  = PI / 5.0;
    let acs = vec2<f32>(cos(an), sin(an));
    let ecs = vec2<f32>(cos(en), sin(en));
    let bn  = (atan2(p_in.x, p_in.y) % an) - 0.5 * an;
    var p   = length(p_in) * vec2<f32>(cos(bn), abs(sin(bn)));
    p      -= 0.65 * acs;
    p      += ecs * clamp(-dot(p, ecs), 0.0, 0.65 * acs.y / ecs.y);
    return length(p) * sign(p.x);
}

fn sdf_pentagon(p_in: vec2<f32>) -> f32 {
    let k = vec3<f32>(0.809016994, 0.587785252, 0.726542528);
    var p = vec2<f32>(abs(p_in.x), p_in.y);
    p    -= 2.0 * min(dot(vec2<f32>(-k.x, k.y), p), 0.0) * vec2<f32>(-k.x, k.y);
    p    -= 2.0 * min(dot(vec2<f32>( k.x, k.y), p), 0.0) * vec2<f32>( k.x, k.y);
    p    -= vec2<f32>(clamp(p.x, -0.7 * k.z, 0.7 * k.z), 0.7);
    return length(p) * sign(p.y);
}

fn sdf_hexagon(p_in: vec2<f32>) -> f32 {
    let k = vec3<f32>(-0.866025404, 0.5, 0.577350269);
    var p = abs(p_in);
    p    -= 2.0 * min(dot(k.xy, p), 0.0) * k.xy;
    p    -= vec2<f32>(clamp(p.x, -k.z * 0.75, k.z * 0.75), 0.75);
    return length(p) * sign(p.y);
}

// ── Fragment shader ───────────────────────────────────────────────────────────

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let aa       = clamp(2.5 / series_pc.point_size, 0.01, 0.15);
    let stroke_w = clamp(4.0 / series_pc.point_size, 0.08, 0.25);
    let mt       = series_pc.marker_type;

    var d         = 0.0;
    var is_filled = true;

    switch mt {
        case 1u: { d = length(in.v_uv) - 0.55; is_filled = true; }
        case 2u: { d = sdf_circle(in.v_uv);    is_filled = false; }
        case 3u: { d = sdf_plus(in.v_uv, 0.15);  is_filled = true; }
        case 4u: { d = sdf_cross(in.v_uv, 0.15); is_filled = true; }
        case 5u: { d = sdf_star(in.v_uv);       is_filled = true; }
        case 6u: { d = sdf_square(in.v_uv);     is_filled = false; }
        case 7u: { d = sdf_diamond(in.v_uv);    is_filled = false; }
        case 8u: { d = sdf_triangle_up(in.v_uv);    is_filled = false; }
        case 9u: { d = sdf_triangle_down(in.v_uv);  is_filled = false; }
        case 10u: { d = sdf_triangle_left(in.v_uv);  is_filled = false; }
        case 11u: { d = sdf_triangle_right(in.v_uv); is_filled = false; }
        case 12u: { d = sdf_pentagon(in.v_uv); is_filled = false; }
        case 13u: { d = sdf_hexagon(in.v_uv);  is_filled = false; }
        case 14u: { d = sdf_circle(in.v_uv);  is_filled = true; }
        case 15u: { d = sdf_square(in.v_uv);  is_filled = true; }
        case 16u: { d = sdf_diamond(in.v_uv); is_filled = true; }
        case 17u: { d = sdf_triangle_up(in.v_uv); is_filled = true; }
        default:  { d = sdf_circle(in.v_uv);  is_filled = true; }
    }

    var alpha = 0.0;
    if is_filled {
        alpha = smoothstep(aa, -aa, d);
    } else {
        let outer = smoothstep(aa, -aa, d);
        let inner = smoothstep(-aa, aa, d + stroke_w);
        alpha = outer * inner;
    }

    if alpha < 0.002 { discard; }

    return vec4<f32>(series_pc.color.rgb, series_pc.color.a * series_pc.opacity * alpha);
}
