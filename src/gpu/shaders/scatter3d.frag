#version 450

// Per-series push constant — must match SeriesPushConstants in backend.hpp
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

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec3 v_model_pos;

layout(location = 0) out vec4 out_color;

// ─── SDF helpers ─────────────────────────────────────────────────────────────
// All shapes are centered at origin, fitting within the -1..1 UV quad.
// Shapes are sized to ~0.8 radius for consistent visual weight.

float sdf_circle(vec2 p) {
    return length(p) - 0.85;
}

float sdf_square(vec2 p) {
    vec2 d = abs(p) - vec2(0.7);
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float sdf_diamond(vec2 p) {
    vec2 d = abs(p);
    return (d.x + d.y) * 0.7071 - 0.6;
}

float sdf_triangle_up(vec2 p) {
    // Equilateral triangle pointing up, centered
    p.y += 0.15; // shift down to visually center
    float k = sqrt(3.0);
    p.x = abs(p.x) - 0.75;
    p.y = p.y + 0.75 / k;
    if (p.x + k * p.y > 0.0) p = vec2(p.x - k * p.y, -k * p.x - p.y) / 2.0;
    p.x -= clamp(p.x, -1.5, 0.0);
    return -length(p) * sign(p.y);
}

float sdf_triangle_down(vec2 p) {
    return sdf_triangle_up(vec2(p.x, -p.y));
}

float sdf_triangle_left(vec2 p) {
    return sdf_triangle_up(vec2(p.y, -p.x));
}

float sdf_triangle_right(vec2 p) {
    return sdf_triangle_up(vec2(-p.y, p.x));
}

float sdf_plus(vec2 p, float arm_w) {
    vec2 d = abs(p);
    return min(max(d.x - arm_w, d.y - 0.75),
               max(d.x - 0.75, d.y - arm_w));
}

float sdf_cross(vec2 p, float arm_w) {
    vec2 r = vec2(p.x - p.y, p.x + p.y) * 0.7071;
    return sdf_plus(r, arm_w);
}

float sdf_star(vec2 p) {
    // 5-pointed star — Inigo Quilez sdStar(p, r, n=5, m=3).
    // Points up (+y).  an = PI/n is half the angle between points.
    const float PI = 3.14159265;
    const float r  = 0.9;
    float an = PI / 5.0;
    float en = PI / 3.0;
    vec2 acs = vec2(cos(an), sin(an));
    vec2 ecs = vec2(cos(en), sin(en));
    float bn = mod(atan(p.x, p.y), 2.0 * an) - an;
    p = length(p) * vec2(cos(bn), abs(sin(bn)));
    p -= r * acs;
    p += ecs * clamp(-dot(p, ecs), 0.0, r * acs.y / ecs.y);
    return length(p) * sign(p.x);
}

float sdf_pentagon(vec2 p) {
    const float PI = 3.14159265;
    const vec3 k = vec3(0.809016994, 0.587785252, 0.726542528); // cos/sin/tan of pi/5
    // The IQ pentagon has its flat edge at +y; flip so the apex points up (+y).
    p.y = -p.y;
    p.x = abs(p.x);
    p -= 2.0 * min(dot(vec2(-k.x, k.y), p), 0.0) * vec2(-k.x, k.y);
    p -= 2.0 * min(dot(vec2( k.x, k.y), p), 0.0) * vec2( k.x, k.y);
    p -= vec2(clamp(p.x, -0.7 * k.z, 0.7 * k.z), 0.7);
    return length(p) * sign(p.y);
}

float sdf_hexagon(vec2 p) {
    const vec3 k = vec3(-0.866025404, 0.5, 0.577350269);
    p = abs(p);
    p -= 2.0 * min(dot(k.xy, p), 0.0) * k.xy;
    p -= vec2(clamp(p.x, -k.z * 0.75, k.z * 0.75), 0.75);
    return length(p) * sign(p.y);
}

// ─── Main ────────────────────────────────────────────────────────────────────

void main() {
    // Discard points outside the bounding box
    const float BOX_HS = 3.0; // must match Axes3D::box_half_size()
    if (any(greaterThan(abs(v_model_pos), vec3(BOX_HS)))) discard;

    // AA width in UV space — scale by point size for resolution-independent smoothing
    // Larger point_size → smaller AA band in UV → sharper edges
    float aa = clamp(2.5 / point_size, 0.01, 0.15);

    float d;
    bool is_filled = true;
    bool has_edge = false;
    // Stroke width in UV space — visible and crisp at all sizes
    float stroke_w = clamp(4.0 / point_size, 0.08, 0.25);

    // marker_type enum values:
    // 0=None, 1=Point, 2=Circle, 3=Plus, 4=Cross, 5=Star,
    // 6=Square, 7=Diamond, 8=TriUp, 9=TriDown, 10=TriLeft, 11=TriRight,
    // 12=Pentagon, 13=Hexagon,
    // 14=FilledCircle, 15=FilledSquare, 16=FilledDiamond, 17=FilledTriUp
    // 18=default scatter dot (internal): filled circle with a contrasting edge

    switch (marker_type) {
        case 1u: // Point — small filled circle
            d = length(v_uv) - 0.55;
            is_filled = true;
            break;
        case 2u: // Circle (outline)
            d = sdf_circle(v_uv);
            is_filled = false;
            break;
        case 3u: // Plus
            d = sdf_plus(v_uv, 0.15);
            is_filled = true;
            break;
        case 4u: // Cross (X)
            d = sdf_cross(v_uv, 0.15);
            is_filled = true;
            break;
        case 5u: // Star
            d = sdf_star(v_uv);
            is_filled = true;
            break;
        case 6u: // Square (outline)
            d = sdf_square(v_uv);
            is_filled = false;
            break;
        case 7u: // Diamond (outline)
            d = sdf_diamond(v_uv);
            is_filled = false;
            break;
        case 8u: // Triangle Up (outline)
            d = sdf_triangle_up(v_uv);
            is_filled = false;
            break;
        case 9u: // Triangle Down (outline)
            d = sdf_triangle_down(v_uv);
            is_filled = false;
            break;
        case 10u: // Triangle Left (outline)
            d = sdf_triangle_left(v_uv);
            is_filled = false;
            break;
        case 11u: // Triangle Right (outline)
            d = sdf_triangle_right(v_uv);
            is_filled = false;
            break;
        case 12u: // Pentagon (outline)
            d = sdf_pentagon(v_uv);
            is_filled = false;
            break;
        case 13u: // Hexagon (outline)
            d = sdf_hexagon(v_uv);
            is_filled = false;
            break;
        case 14u: // Filled Circle
            d = sdf_circle(v_uv);
            is_filled = true;
            break;
        case 15u: // Filled Square
            d = sdf_square(v_uv);
            is_filled = true;
            break;
        case 16u: // Filled Diamond
            d = sdf_diamond(v_uv);
            is_filled = true;
            break;
        case 17u: // Filled Triangle Up
            d = sdf_triangle_up(v_uv);
            is_filled = true;
            break;
        case 18u: // Default scatter dot (no explicit marker style)
            d = sdf_circle(v_uv);
            is_filled = true;
            has_edge = true;
            break;
        default: // Fallback: filled circle
            d = sdf_circle(v_uv);
            is_filled = true;
            break;
    }

    float alpha;
    if (is_filled) {
        // Filled shape: smooth edge
        alpha = smoothstep(aa, -aa, d);
    } else {
        // Outline shape: annular ring with smooth inner and outer edges
        float outer = smoothstep(aa, -aa, d);
        float inner = smoothstep(-aa, aa, d + stroke_w);
        alpha = outer * inner;
    }

    if (alpha < 0.002) discard;

    vec3 base_color = color.rgb;
    // Default scatter dot: tint a ~1 px rim towards white (_pad2[1] > 0) or
    // black (_pad2[1] < 0) so dark or light colors stay visible on the canvas.
    if (has_edge) {
        float edge_w = clamp(1.0 / (point_size * 0.5 + 1.0), 0.06, 0.3);
        float rim = smoothstep(-edge_w - aa, -edge_w + aa, d);
        vec3 target = _pad2[1] >= 0.0 ? vec3(1.0) : vec3(0.0);
        base_color = mix(base_color, mix(base_color, target, abs(_pad2[1])), rim);
    }

    out_color = vec4(base_color, color.a * opacity * alpha);
}
