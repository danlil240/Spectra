// text.wgsl — WGSL port of text.vert + text.frag for the WebGPU backend.
// Renders MSDF/atlas-based text quads.  Vertex attributes carry per-glyph
// position, UV coordinates, and packed RGBA color.

// ── Bind group 0: per-frame uniform ─────────────────────────────────────────

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

// ── Bind group 1: font atlas texture and sampler ─────────────────────────────

@group(1) @binding(0) var font_tex: texture_2d<f32>;
@group(1) @binding(1) var font_smp: sampler;

// ── Vertex shader ─────────────────────────────────────────────────────────────

struct VertexOutput {
    @builtin(position) clip_pos   : vec4<f32>,
    @location(0)       frag_uv    : vec2<f32>,
    @location(1)       frag_color : vec4<f32>,
};

@vertex
fn vs_main(
    @location(0) in_position: vec3<f32>,
    @location(1) in_uv      : vec2<f32>,
    @location(2) in_color   : u32,
) -> VertexOutput {
    var out: VertexOutput;

    out.frag_uv = in_uv;

    // Unpack RGBA from uint32 (same byte order as ImGui/VulkanBackend)
    out.frag_color = vec4<f32>(
        f32((in_color >>  0u) & 0xFFu) / 255.0,
        f32((in_color >>  8u) & 0xFFu) / 255.0,
        f32((in_color >> 16u) & 0xFFu) / 255.0,
        f32((in_color >> 24u) & 0xFFu) / 255.0,
    );

    // Screen-space ortho projection; Z passes through as NDC depth.
    out.clip_pos   = frame.projection * vec4<f32>(in_position.xy, 0.0, 1.0);
    out.clip_pos.z = in_position.z;
    return out;
}

// ── Fragment shader ───────────────────────────────────────────────────────────

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let coverage = textureSample(font_tex, font_smp, in.frag_uv).a;
    if coverage < 0.005 { discard; }

    let alpha = smoothstep(0.05, 0.55, coverage);
    return vec4<f32>(in.frag_color.rgb, in.frag_color.a * alpha);
}
