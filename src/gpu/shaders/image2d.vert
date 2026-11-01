#version 450

// 2D image quad (ImageSeries).  No vertex buffer: the 6 vertices of the quad
// come from gl_VertexIndex and the corners from the push constants.

// Per-frame UBO (set 0, binding 0)
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 projection;
    mat4 view;
    mat4 model;
    vec2 viewport_size;
    float time;
    float _pad0;
    vec3 camera_pos;
    float near_plane;
    vec3 light_dir;
    float far_plane;
};

// Per-series push constant — must match SeriesPushConstants in backend.hpp.
// Image use of the fields (see Renderer::render_series, SeriesType::Image2D):
//   dash_pattern[0..1] = quad x0, x1; dash_pattern[2..3] = y0, y1 (relative
//   to the view centre, like every other 2D vertex); dash_pattern[4..5] =
//   vmin, vmax; dash_count = flags; color.a = opacity.
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

// uv = (0, 0) at (x0, y0), (1, 1) at (x1, y1).
layout(location = 0) out vec2 v_uv;

void main() {
    int corner_map[6] = int[6](0, 1, 2, 2, 1, 3);
    int corner = corner_map[gl_VertexIndex % 6];
    vec2 uv = vec2(float(corner & 1), float(corner >> 1));
    vec2 pos = vec2(mix(dash_pattern[0], dash_pattern[1], uv.x),
                    mix(dash_pattern[2], dash_pattern[3], uv.y));
    v_uv = uv;
    gl_Position = projection * view * model * vec4(pos, 0.0, 1.0);
}
