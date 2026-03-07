#version 450

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

layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec3 v_view_dir;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 N = normalize(v_normal);
    vec3 V = normalize(v_view_dir);

    if (dot(N, V) < 0.0) {
        N = -N;
    }

    float ambient_strength  = 0.2;
    float specular_strength = 0.3;

    vec3 L = light_dir;
    if (dot(L, L) < 0.001) {
        L = vec3(0.5, 0.7, 1.0);
    }
    L = normalize(mat3(model) * L);

    float ambient = ambient_strength;
    float NdotL = max(dot(N, L), 0.0);
    float diffuse = NdotL * 0.65;

    float shininess = 32.0;
    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float specular = pow(NdotH, shininess) * specular_strength;

    float intensity = min(ambient + diffuse + specular, 1.0);

    out_color = vec4(color.rgb * intensity, color.a * opacity);
}
