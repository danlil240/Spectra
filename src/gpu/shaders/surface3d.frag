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

// Colormap lookup: dash_count encodes colormap type (1=Viridis..7=Grayscale)
// dash_pattern[0] = z_min, dash_pattern[1] = z_max
vec3 apply_colormap(int cm_type, float t) {
    t = clamp(t, 0.0, 1.0);
    switch (cm_type) {
        case 1: { // Viridis: dark purple -> teal -> yellow
            float r = clamp(-0.35 + 1.7*t - 0.9*t*t + 0.55*t*t*t, 0.0, 1.0);
            float g = clamp(-0.05 + 0.7*t + 0.3*t*t, 0.0, 1.0);
            float b = clamp(0.33 + 0.7*t - 1.6*t*t + 0.6*t*t*t, 0.0, 1.0);
            return vec3(r, g, b);
        }
        case 2: { // Plasma: dark blue -> magenta -> yellow
            float r = clamp(0.05 + 2.2*t - 1.3*t*t, 0.0, 1.0);
            float g = clamp(-0.2 + 1.2*t, 0.0, 1.0);
            float b = clamp(0.53 + 0.5*t - 2.0*t*t + 1.0*t*t*t, 0.0, 1.0);
            return vec3(r, g, b);
        }
        case 3: { // Inferno: black -> red -> yellow
            float r = clamp(-0.1 + 2.5*t - 1.5*t*t, 0.0, 1.0);
            float g = clamp(-0.3 + 1.5*t, 0.0, 1.0);
            float b = clamp(0.1 + 2.0*t - 3.5*t*t + 1.5*t*t*t, 0.0, 1.0);
            return vec3(r, g, b);
        }
        case 4: { // Magma: black -> purple -> orange -> white
            float r = clamp(-0.05 + 2.0*t - 0.8*t*t, 0.0, 1.0);
            float g = clamp(-0.3 + 1.3*t + 0.1*t*t, 0.0, 1.0);
            float b = clamp(0.15 + 1.5*t - 2.5*t*t + 1.5*t*t*t, 0.0, 1.0);
            return vec3(r, g, b);
        }
        case 5: { // Jet: blue -> cyan -> green -> yellow -> red
            float r = clamp(1.5 - abs(t - 0.75) * 4.0, 0.0, 1.0);
            float g = clamp(1.5 - abs(t - 0.5) * 4.0, 0.0, 1.0);
            float b = clamp(1.5 - abs(t - 0.25) * 4.0, 0.0, 1.0);
            return vec3(r, g, b);
        }
        case 6: { // Coolwarm: blue -> white -> red
            float r = clamp(0.23 + 1.5*t - 0.7*t*t, 0.0, 1.0);
            float g = clamp(0.3 + 1.2*t - 1.5*t*t, 0.0, 1.0);
            float b = clamp(0.75 - 0.5*t - 0.2*t*t, 0.0, 1.0);
            return vec3(r, g, b);
        }
        case 7: // Grayscale
            return vec3(t);
        default:
            return vec3(0.5);
    }
}

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

    // Determine base color: use colormap if dash_count > 0, else push constant color
    vec3 base_color = color.rgb;
    if (dash_count > 0) {
        float z_min = dash_pattern[0];
        float z_max = dash_pattern[1];
        float z_range = z_max - z_min;
        float t = (z_range > 0.0001) ? (v_world_pos.z - z_min) / z_range : 0.5;
        base_color = apply_colormap(dash_count, t);
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
    // marker_size encodes shininess when used for surface (0 = use default 32)
    float shininess = (marker_type == 0u && marker_size > 1.0) ? marker_size : 32.0;
    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float specular = pow(NdotH, shininess) * specular_strength;

    // Secondary fill light from opposite side (softer)
    vec3 L2 = normalize(mat3(model) * vec3(-0.3, -0.5, 0.4));
    float fill = max(dot(N, L2), 0.0) * 0.15;

    // Combine lighting
    float lighting = ambient + diffuse + fill;
    vec3 lit_color = base_color * lighting + vec3(specular);

    // Clamp to valid range
    lit_color = clamp(lit_color, 0.0, 1.0);

    out_color = vec4(lit_color, color.a * opacity);
}
