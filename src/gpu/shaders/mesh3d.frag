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
    // Flat face normal, computed in uniform control flow (before any discard)
    vec3 face_normal = cross(dFdx(v_world_pos), dFdy(v_world_pos));

    // Discard fragments outside the bounding box
    const float BOX_HS = 3.0; // must match Axes3D::box_half_size()
    if (any(greaterThan(abs(v_world_pos), vec3(BOX_HS)))) discard;

    // Normalize interpolated vectors. Meshes supplied without normals (all
    // zero) fall back to the flat face normal from screen-space derivatives.
    vec3 N = v_normal;
    if (!(dot(N, N) > 1e-8)) {
        N = face_normal;
    }
    N = normalize(N);
    vec3 V = normalize(v_view_dir);

    // Ensure normal faces the viewer (for back-face rendering)
    if (dot(N, V) < 0.0) {
        N = -N;
    }

    vec3 base_color = color.rgb;

    // Lighting disabled (Axes3D::lighting_enabled(false)): the renderer sends a
    // zero light_dir, and the surface is drawn in its flat base colour.
    if (dot(light_dir, light_dir) < 1e-12) {
        out_color = vec4(base_color, color.a * opacity);
        return;
    }

    // Decode material properties from push constants:
    // _pad2[0] = ambient strength  (0 = use default 0.40)
    // _pad2[1] = specular strength (0 = use default 0.12)
    // Defaults keep the base colour readable: a high ambient floor plus a soft
    // fill light so faces turned away from the key light keep their hue, and a
    // weak, base-tinted specular so highlights do not bleach the colour to white.
    float ambient_strength  = _pad2[0] > 0.001 ? _pad2[0] : 0.40;
    float specular_strength = _pad2[1] > 0.001 ? _pad2[1] : 0.12;

    // Primary (key) light direction from UBO (never zero here, see above).
    // Transform from data space into model space to match normals.
    vec3 L = normalize(mat3(model) * light_dir);

    // Diffuse (Lambertian) from the key light
    float diffuse = max(dot(N, L), 0.0) * 0.55;

    // Soft fill light from the opposite side so shadowed faces are not crushed
    vec3 L2 = normalize(mat3(model) * vec3(-0.3, -0.5, 0.4));
    float fill = max(dot(N, L2), 0.0) * 0.25;

    // Shading only scales the base colour (hue and chroma ratios are preserved)
    // and never brightens it past the true colour, so colormaps stay faithful.
    float shade = min(ambient_strength + diffuse + fill, 1.0);

    // Specular (Blinn-Phong), tinted by the base colour.
    // marker_size encodes shininess (0 = use default 32)
    float shininess = (marker_type == 0u && marker_size > 1.0) ? marker_size : 32.0;
    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float specular = pow(NdotH, shininess) * specular_strength;
    vec3 spec_color = mix(base_color, vec3(1.0), 0.35) * specular;

    vec3 lit_color = clamp(base_color * shade + spec_color, 0.0, 1.0);

    out_color = vec4(lit_color, color.a * opacity);
}
