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

// Evaluate c0 + c1 t + ... + c6 t^6 (Horner form)
vec3 cm_poly(float t, vec3 c0, vec3 c1, vec3 c2, vec3 c3, vec3 c4, vec3 c5, vec3 c6) {
    return c0 + t * (c1 + t * (c2 + t * (c3 + t * (c4 + t * (c5 + t * c6)))));
}

// Colormap lookup: dash_count encodes colormap type (1=Viridis..7=Grayscale)
// dash_pattern[0] = z_min, dash_pattern[1] = z_max
vec3 apply_colormap(int cm_type, float t) {
    t = clamp(t, 0.0, 1.0);
    switch (cm_type) {
        // Viridis, plasma, inferno and magma: degree-6 polynomial fits of the
        // matplotlib maps (max error about 0.02 per channel), matching
        // SurfaceSeries::sample_colormap on the CPU.
        case 1: // Viridis: dark purple -> teal -> green -> yellow
            return clamp(cm_poly(t,
                vec3(0.2777273272234177, 0.005407344544966578, 0.3340998053353061),
                vec3(0.1050930431085774, 1.404613529898575, 1.384590162594685),
                vec3(-0.3308618287255563, 0.214847559468213, 0.09509516302823659),
                vec3(-4.634230498983486, -5.799100973351585, -19.33244095627987),
                vec3(6.228269936347081, 14.17993336680509, 56.69055260068105),
                vec3(4.776384997670288, -13.74514537774601, -65.35303263337234),
                vec3(-5.435455855934631, 4.645852612178535, 26.3124352495832)), 0.0, 1.0);
        case 2: // Plasma: dark blue -> magenta -> yellow
            return clamp(cm_poly(t,
                vec3(0.05873234392399702, 0.02333670892565664, 0.5433401826748754),
                vec3(2.176514634195958, 0.2383834171260182, 0.7539604599784036),
                vec3(-2.689460476458034, -7.455851135738909, 3.110799939717086),
                vec3(6.130348345893603, 42.3461881477227, -28.51885465332158),
                vec3(-11.10743619062271, -82.66631109428045, 60.13984767418263),
                vec3(10.02306557647065, 71.41361770095349, -54.07218655560067),
                vec3(-3.658713842777788, -22.93153465461149, 18.19190778539828)), 0.0, 1.0);
        case 3: // Inferno: black -> red -> yellow
            return clamp(cm_poly(t,
                vec3(0.0002189403691192265, 0.001651004631001012, -0.01948089843709184),
                vec3(0.1065134194856116, 0.5639564367884091, 3.932712388889277),
                vec3(11.60249308247187, -3.972853965665698, -15.9423941062914),
                vec3(-41.70399613139459, 17.43639888205313, 44.35414519872813),
                vec3(77.162935699427, -33.40235894210092, -81.80730925738993),
                vec3(-71.31942824499214, 32.62606426397723, 73.20951985803202),
                vec3(25.13112622477341, -12.24266895238567, -23.07032500287172)), 0.0, 1.0);
        case 4: // Magma: black -> purple -> orange -> pale yellow
            return clamp(cm_poly(t,
                vec3(-0.002136485053939582, -0.000749655052795221, -0.005386127855323933),
                vec3(0.2516605407371642, 0.6775232436837668, 2.494026599312351),
                vec3(8.353717279216625, -3.577719514958484, 0.3144679030132573),
                vec3(-27.66873308576866, 14.26473078096533, -13.64921318813922),
                vec3(52.17613981234068, -27.94360607168351, 12.94416944238394),
                vec3(-50.76852536473588, 29.04658282127291, 4.23415299384598),
                vec3(18.65570506591883, -11.48977351997711, -5.601961508734096)), 0.0, 1.0);
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
    // _pad2[0] = ambient strength  (0 = use default 0.40)
    // _pad2[1] = specular strength (0 = use default 0.12)
    // Defaults keep the base colour readable: a high ambient floor plus a soft
    // fill light so faces turned away from the key light keep their hue, and a
    // weak, base-tinted specular so highlights do not bleach the colour to white.
    float ambient_strength  = _pad2[0] > 0.001 ? _pad2[0] : 0.40;
    float specular_strength = _pad2[1] > 0.001 ? _pad2[1] : 0.12;

    // Primary (key) light direction from UBO (fallback to default if zero).
    // Transform from data space into model space to match normals.
    vec3 L = light_dir;
    if (dot(L, L) < 0.001) {
        L = vec3(0.5, 0.7, 1.0);
    }
    L = normalize(mat3(model) * L);

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
