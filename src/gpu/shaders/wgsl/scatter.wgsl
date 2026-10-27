// scatter.wgsl — WGSL port of scatter.vert, scatter_colormap.vert and
// scatter.frag for the WebGPU backend.
// Renders instanced SDF scatter markers.  Each instance is one data point read
// from a storage buffer; 6 vertices per instance form a screen-space quad.
//
// Two vertex entry points share the fragment shader:
//   vs_main     — PipelineType::Scatter, SSBO of vec2 (x, y)
//   vs_colormap — PipelineType::ScatterColormap, SSBO of vec4 (x, y, value, pad)
// The SSBO is declared as raw floats so both strides can use one binding.

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

@group(1) @binding(0) var<storage, read> vertex_data : array<f32>;

// ── Vertex output / fragment input ──────────────────────────────────────────

struct VertexOutput {
    @builtin(position)                    clip_pos     : vec4<f32>,
    @location(0)                          v_uv         : vec2<f32>,
    @location(1) @interpolate(flat)       v_color_value: f32,
};

// ── Vertex shader ─────────────────────────────────────────────────────────────

// Screen-space marker quad around a data-space center.
fn marker_quad(vertex_index: u32, center_in: vec2<f32>) -> VertexOutput {
    var out: VertexOutput;

    let tri_vert   = i32(vertex_index) % 6;
    let corner_map = array<i32, 6>(0, 1, 2, 2, 1, 3);
    let corner     = corner_map[tri_vert];

    let data_offset  = vec2<f32>(series_pc.data_offset_x, series_pc.data_offset_y);
    let center       = center_in + data_offset;

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
    // WebGPU clip Y points up, so +offset.y is already "up" on screen (the
    // Vulkan shader flips it because its clip Y points down).
    out.v_uv          = offset;
    out.v_color_value = 0.0;
    return out;
}

@vertex
fn vs_main(
    @builtin(vertex_index)   vertex_index  : u32,
    @builtin(instance_index) instance_index: u32,
) -> VertexOutput {
    let i = instance_index * 2u;
    return marker_quad(vertex_index, vec2<f32>(vertex_data[i], vertex_data[i + 1u]));
}

@vertex
fn vs_colormap(
    @builtin(vertex_index)   vertex_index  : u32,
    @builtin(instance_index) instance_index: u32,
) -> VertexOutput {
    let i   = instance_index * 4u;
    var out = marker_quad(vertex_index, vec2<f32>(vertex_data[i], vertex_data[i + 1u]));
    out.v_color_value = vertex_data[i + 2u];
    return out;
}

// ── Colormaps (same control points as scatter.frag) ──────────────────────────

fn mix3(a: vec3<f32>, b: vec3<f32>, c: vec3<f32>, t: f32) -> vec3<f32> {
    if t < 0.5 {
        return mix(a, b, t * 2.0);
    }
    return mix(b, c, (t - 0.5) * 2.0);
}

fn apply_colormap(cm_type: i32, t_in: f32) -> vec3<f32> {
    let t = clamp(t_in, 0.0, 1.0);
    switch cm_type {
        case 1: {   // Viridis
            return mix3(vec3<f32>(0.267, 0.005, 0.329), vec3<f32>(0.128, 0.567, 0.551),
                        vec3<f32>(0.993, 0.906, 0.144), t);
        }
        case 2: {   // Plasma
            return mix3(vec3<f32>(0.050, 0.030, 0.528), vec3<f32>(0.798, 0.280, 0.470),
                        vec3<f32>(0.940, 0.975, 0.131), t);
        }
        case 3: {   // Inferno
            return mix3(vec3<f32>(0.001, 0.000, 0.014), vec3<f32>(0.735, 0.216, 0.330),
                        vec3<f32>(0.988, 0.998, 0.645), t);
        }
        case 4: {   // Magma
            return mix3(vec3<f32>(0.001, 0.000, 0.014), vec3<f32>(0.716, 0.215, 0.475),
                        vec3<f32>(0.987, 0.991, 0.750), t);
        }
        case 5: {   // Jet
            return clamp(vec3<f32>(1.5) - abs(4.0 * t - vec3<f32>(3.0, 2.0, 1.0)),
                         vec3<f32>(0.0), vec3<f32>(1.0));
        }
        case 6: {   // Coolwarm
            return mix3(vec3<f32>(0.230, 0.299, 0.754), vec3<f32>(0.865, 0.865, 0.865),
                        vec3<f32>(0.706, 0.016, 0.150), t);
        }
        case 7: {   // Grayscale
            return vec3<f32>(t);
        }
        default: {
            return series_pc.color.rgb;
        }
    }
}

// WGSL has no isnan/isinf; NaN fails every comparison with itself.
fn is_finite(v: f32) -> bool {
    return v == v && abs(v) <= 0x1.fffffep+127f;
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
    // 5-pointed star — Inigo Quilez sdStar(p, r, n=5, m=3), pointing up (+y).
    let PI  = 3.14159265;
    let r   = 0.9;
    let an  = PI / 5.0;
    let en  = PI / 3.0;
    let acs = vec2<f32>(cos(an), sin(an));
    let ecs = vec2<f32>(cos(en), sin(en));
    // WGSL `%` truncates toward zero, so use a floor-based modulo.
    let a   = atan2(p_in.x, p_in.y);
    let bn  = (a - 2.0 * an * floor(a / (2.0 * an))) - an;
    var p   = length(p_in) * vec2<f32>(cos(bn), abs(sin(bn)));
    p      -= r * acs;
    p      += ecs * clamp(-dot(p, ecs), 0.0, r * acs.y / ecs.y);
    return length(p) * sign(p.x);
}

fn sdf_pentagon(p_in: vec2<f32>) -> f32 {
    let k = vec3<f32>(0.809016994, 0.587785252, 0.726542528);
    // The IQ pentagon has its flat edge at +y; flip so the apex points up (+y).
    var p = vec2<f32>(abs(p_in.x), -p_in.y);
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
    var has_edge  = false;

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
        // 18 = default scatter dot (no explicit marker): filled circle with a
        // contrasting edge.
        case 18u: { d = sdf_circle(in.v_uv);  is_filled = true; has_edge = true; }
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

    var base_color = series_pc.color.rgb;
    if series_pc._pad2.x > 0.5 && series_pc.dash_count > 0 && is_finite(in.v_color_value) {
        let value_min = series_pc.dash_pattern[0];
        let value_max = series_pc.dash_pattern[1];
        let range     = value_max - value_min;
        var t         = 0.5;
        if abs(range) > 1e-20 {
            t = (in.v_color_value - value_min) / range;
        }
        base_color = apply_colormap(series_pc.dash_count, t);
    }

    // Default scatter dot: tint a ~1 px rim towards white (_pad2.y > 0) or
    // black (_pad2.y < 0) so dark or light colors stay visible on the canvas.
    if has_edge {
        let edge_w = clamp(1.0 / (series_pc.point_size * 0.5 + 1.0), 0.06, 0.3);
        let rim    = smoothstep(-edge_w - aa, -edge_w + aa, d);
        var edge_target = vec3<f32>(0.0);
        if series_pc._pad2.y >= 0.0 {
            edge_target = vec3<f32>(1.0);
        }
        base_color = mix(base_color, mix(base_color, edge_target, abs(series_pc._pad2.y)), rim);
    }

    return vec4<f32>(base_color, series_pc.color.a * series_pc.opacity * alpha);
}
