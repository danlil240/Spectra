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
    // Discard fragments outside the bounding box
    const float BOX_HS = 3.0; // must match Axes3D::box_half_size()
    if (any(greaterThan(abs(v_world_pos), vec3(BOX_HS)))) discard;

    // Normalize interpolated vectors
    vec3 N = normalize(v_normal);
    vec3 V = normalize(v_view_dir);

    // Ensure normal faces the viewer (for back-face rendering)
    if (dot(N, V) < 0.0) {
        N = -N;
    }

    // Decode material properties from push constants:
    // _pad2[0] = ambient strength  (0 = use default 0.15)
    // _pad2[1] = specular strength (0 = use default 0.3)
    float ambient_strength  = _pad2[0] > 0.001 ? _pad2[0] : 0.15;
    float specular_strength = _pad2[1] > 0.001 ? _pad2[1] : 0.3;

    // Primary light direction from UBO (fallback to default if zero).
    // Transform from data space into model space to match normals.
    vec3 L = light_dir;
    if (dot(L, L) < 0.001) {
        L = vec3(0.5, 0.7, 1.0);
    }
    L = normalize(mat3(model) * L);

    // Ambient
    float ambient = ambient_strength;

    // Diffuse (Lambertian)
    float NdotL = max(dot(N, L), 0.0);
    float diffuse = NdotL * 0.65;

    // Specular (Blinn-Phong)
    // marker_size encodes shininess when used for mesh (0 = use default 32)
    float shininess = (marker_type == 0u && marker_size > 1.0) ? marker_size : 32.0;
    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float specular = pow(NdotH, shininess) * specular_strength;

    // Secondary fill light from opposite side (softer)
    vec3 L2 = normalize(mat3(model) * vec3(-0.3, -0.5, 0.4));
    float fill = max(dot(N, L2), 0.0) * 0.15;

    // Combine lighting
    float lighting = ambient + diffuse + fill;
    vec3 lit_color = color.rgb * lighting + vec3(specular);

    // Clamp to valid range
    lit_color = clamp(lit_color, 0.0, 1.0);

    out_color = vec4(lit_color, color.a * opacity);
}
