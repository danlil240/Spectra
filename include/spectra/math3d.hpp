#pragma once

#include <cmath>
#include <cstring>

namespace spectra
{

// ─── vec3 ────────────────────────────────────────────────────────────────────

struct vec3
{
    double x = 0.0, y = 0.0, z = 0.0;

    constexpr vec3() = default;
    constexpr vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    constexpr vec3  operator+(vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    constexpr vec3  operator-(vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    constexpr vec3  operator*(double s) const { return {x * s, y * s, z * s}; }
    constexpr vec3  operator/(double s) const { return {x / s, y / s, z / s}; }
    constexpr vec3  operator-() const { return {-x, -y, -z}; }
    constexpr vec3& operator+=(vec3 b)
    {
        x += b.x;
        y += b.y;
        z += b.z;
        return *this;
    }
    constexpr vec3& operator-=(vec3 b)
    {
        x -= b.x;
        y -= b.y;
        z -= b.z;
        return *this;
    }
    constexpr vec3& operator*=(double s)
    {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
    constexpr bool operator==(vec3 b) const { return x == b.x && y == b.y && z == b.z; }
    constexpr bool operator!=(vec3 b) const { return !(*this == b); }

    double&       operator[](int i) { return (&x)[i]; }
    const double& operator[](int i) const { return (&x)[i]; }
};

inline constexpr vec3 operator*(double s, vec3 v)
{
    return v * s;
}

inline constexpr double vec3_dot(vec3 a, vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline constexpr vec3 vec3_cross(vec3 a, vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline double vec3_length(vec3 v)
{
    return std::sqrt(vec3_dot(v, v));
}

inline double vec3_length_sq(vec3 v)
{
    return vec3_dot(v, v);
}

inline vec3 vec3_normalize(vec3 v)
{
    double len = vec3_length(v);
    return len > 1e-12 ? v / len : vec3{0.0, 0.0, 0.0};
}

inline vec3 vec3_lerp(vec3 a, vec3 b, double t)
{
    return a + (b - a) * t;
}

inline vec3 vec3_min(vec3 a, vec3 b)
{
    return {std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmin(a.z, b.z)};
}

inline vec3 vec3_max(vec3 a, vec3 b)
{
    return {std::fmax(a.x, b.x), std::fmax(a.y, b.y), std::fmax(a.z, b.z)};
}

// ─── vec4 ────────────────────────────────────────────────────────────────────

struct vec4
{
    double x = 0.0, y = 0.0, z = 0.0, w = 0.0;

    constexpr vec4() = default;
    constexpr vec4(double x_, double y_, double z_, double w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr vec4(vec3 v, double w_) : x(v.x), y(v.y), z(v.z), w(w_) {}

    constexpr vec3 xyz() const { return {x, y, z}; }

    constexpr vec4 operator+(vec4 b) const { return {x + b.x, y + b.y, z + b.z, w + b.w}; }
    constexpr vec4 operator-(vec4 b) const { return {x - b.x, y - b.y, z - b.z, w - b.w}; }
    constexpr vec4 operator*(double s) const { return {x * s, y * s, z * s, w * s}; }

    double&       operator[](int i) { return (&x)[i]; }
    const double& operator[](int i) const { return (&x)[i]; }
};

// ─── mat4 ────────────────────────────────────────────────────────────────────
// Column-major layout: m[col][row], stored as float[16].
// Element access: m[col * 4 + row]

struct mat4
{
    float m[16]{};

    constexpr mat4() = default;

    constexpr float&       operator()(int row, int col) { return m[col * 4 + row]; }
    constexpr const float& operator()(int row, int col) const { return m[col * 4 + row]; }

    constexpr bool operator==(const mat4& b) const
    {
        for (int i = 0; i < 16; ++i)
            if (m[i] != b.m[i])
                return false;
        return true;
    }
    constexpr bool operator!=(const mat4& b) const { return !(*this == b); }
};

inline constexpr mat4 mat4_identity()
{
    mat4 r;
    r.m[0]  = 1.0;
    r.m[5]  = 1.0;
    r.m[10] = 1.0;
    r.m[15] = 1.0;
    return r;
}

inline constexpr mat4 mat4_mul(const mat4& a, const mat4& b)
{
    mat4 r;
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            r.m[col * 4 + row] = sum;
        }
    }
    return r;
}

inline constexpr vec4 mat4_mul_vec4(const mat4& m, vec4 v)
{
    return {
        m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12] * v.w,
        m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13] * v.w,
        m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w,
        m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w,
    };
}

inline constexpr mat4 mat4_translate(vec3 t)
{
    mat4 r  = mat4_identity();
    r.m[12] = t.x;
    r.m[13] = t.y;
    r.m[14] = t.z;
    return r;
}

inline constexpr mat4 mat4_scale(vec3 s)
{
    mat4 r;
    r.m[0]  = s.x;
    r.m[5]  = s.y;
    r.m[10] = s.z;
    r.m[15] = 1.0f;
    return r;
}

inline mat4 mat4_rotate_x(float angle_rad)
{
    float c = std::cos(angle_rad), s = std::sin(angle_rad);
    mat4  r = mat4_identity();
    r.m[5]  = c;
    r.m[9]  = -s;
    r.m[6]  = s;
    r.m[10] = c;
    return r;
}

inline mat4 mat4_rotate_y(float angle_rad)
{
    float c = std::cos(angle_rad), s = std::sin(angle_rad);
    mat4  r = mat4_identity();
    r.m[0]  = c;
    r.m[8]  = s;
    r.m[2]  = -s;
    r.m[10] = c;
    return r;
}

inline mat4 mat4_rotate_z(float angle_rad)
{
    float c = std::cos(angle_rad), s = std::sin(angle_rad);
    mat4  r = mat4_identity();
    r.m[0]  = c;
    r.m[4]  = -s;
    r.m[1]  = s;
    r.m[5]  = c;
    return r;
}

inline mat4 mat4_look_at(vec3 eye, vec3 target, vec3 up)
{
    vec3 f = vec3_normalize(target - eye);
    vec3 r = vec3_normalize(vec3_cross(f, up));
    vec3 u = vec3_cross(r, f);

    mat4 m  = mat4_identity();
    m.m[0]  = r.x;
    m.m[4]  = r.y;
    m.m[8]  = r.z;
    m.m[1]  = u.x;
    m.m[5]  = u.y;
    m.m[9]  = u.z;
    m.m[2]  = -f.x;
    m.m[6]  = -f.y;
    m.m[10] = -f.z;
    m.m[12] = -vec3_dot(r, eye);
    m.m[13] = -vec3_dot(u, eye);
    m.m[14] = vec3_dot(f, eye);
    return m;
}

inline mat4 mat4_perspective(float fov_y_rad, float aspect, float near, float far)
{
    float t = std::tan(fov_y_rad * 0.5f);
    mat4  m;
    m.m[0]  = 1.0f / (aspect * t);
    m.m[5]  = -1.0f / t;   // Vulkan Y-flip
    m.m[10] = far / (near - far);
    m.m[11] = -1.0f;
    m.m[14] = (near * far) / (near - far);
    return m;
}

inline mat4 mat4_ortho(float left, float right, float bottom, float top, float near, float far)
{
    mat4  m;
    float rl = right - left;
    float tb = top - bottom;
    float fn = far - near;
    if (rl == 0.0f)
        rl = 1.0f;
    if (tb == 0.0f)
        tb = 1.0f;
    if (fn == 0.0f)
        fn = 1.0f;

    m.m[0]  = 2.0f / rl;
    m.m[5]  = -2.0f / tb;   // Vulkan Y-flip
    m.m[10] = -1.0f / fn;
    m.m[12] = -(right + left) / rl;
    m.m[13] = (top + bottom) / tb;
    m.m[14] = -near / fn;
    m.m[15] = 1.0f;
    return m;
}

inline mat4 mat4_transpose(const mat4& a)
{
    mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[j * 4 + i] = a.m[i * 4 + j];
    return r;
}

inline float mat4_determinant(const mat4& m)
{
    // Laplace expansion along first row
    float a = m.m[0], b = m.m[4], c = m.m[8], d = m.m[12];
    float e = m.m[1], f = m.m[5], g = m.m[9], h = m.m[13];
    float i = m.m[2], j = m.m[6], k = m.m[10], l = m.m[14];
    float n = m.m[3], o = m.m[7], p = m.m[11], q = m.m[15];

    float kq_pl = k * q - p * l;
    float jq_ol = j * q - o * l;
    float jp_ok = j * p - o * k;
    float iq_nl = i * q - n * l;
    float ip_nk = i * p - n * k;
    float io_nj = i * o - n * j;

    return a * (f * kq_pl - g * jq_ol + h * jp_ok) - b * (e * kq_pl - g * iq_nl + h * ip_nk)
           + c * (e * jq_ol - f * iq_nl + h * io_nj) - d * (e * jp_ok - f * ip_nk + g * io_nj);
}

inline mat4 mat4_inverse(const mat4& m)
{
    // Compute adjugate / determinant
    float a00 = m.m[0], a01 = m.m[4], a02 = m.m[8], a03 = m.m[12];
    float a10 = m.m[1], a11 = m.m[5], a12 = m.m[9], a13 = m.m[13];
    float a20 = m.m[2], a21 = m.m[6], a22 = m.m[10], a23 = m.m[14];
    float a30 = m.m[3], a31 = m.m[7], a32 = m.m[11], a33 = m.m[15];

    float b00 = a00 * a11 - a01 * a10;
    float b01 = a00 * a12 - a02 * a10;
    float b02 = a00 * a13 - a03 * a10;
    float b03 = a01 * a12 - a02 * a11;
    float b04 = a01 * a13 - a03 * a11;
    float b05 = a02 * a13 - a03 * a12;
    float b06 = a20 * a31 - a21 * a30;
    float b07 = a20 * a32 - a22 * a30;
    float b08 = a20 * a33 - a23 * a30;
    float b09 = a21 * a32 - a22 * a31;
    float b10 = a21 * a33 - a23 * a31;
    float b11 = a22 * a33 - a23 * a32;

    float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if (std::fabs(det) < 1e-12f)
        return mat4_identity();
    float inv_det = 1.0f / det;

    mat4 r;
    r.m[0]  = (a11 * b11 - a12 * b10 + a13 * b09) * inv_det;
    r.m[1]  = (-a10 * b11 + a12 * b08 - a13 * b07) * inv_det;
    r.m[2]  = (a10 * b10 - a11 * b08 + a13 * b06) * inv_det;
    r.m[3]  = (-a10 * b09 + a11 * b07 - a12 * b06) * inv_det;
    r.m[4]  = (-a01 * b11 + a02 * b10 - a03 * b09) * inv_det;
    r.m[5]  = (a00 * b11 - a02 * b08 + a03 * b07) * inv_det;
    r.m[6]  = (-a00 * b10 + a01 * b08 - a03 * b06) * inv_det;
    r.m[7]  = (a00 * b09 - a01 * b07 + a02 * b06) * inv_det;
    r.m[8]  = (a31 * b05 - a32 * b04 + a33 * b03) * inv_det;
    r.m[9]  = (-a30 * b05 + a32 * b02 - a33 * b01) * inv_det;
    r.m[10] = (a30 * b04 - a31 * b02 + a33 * b00) * inv_det;
    r.m[11] = (-a30 * b03 + a31 * b01 - a32 * b00) * inv_det;
    r.m[12] = (-a21 * b05 + a22 * b04 - a23 * b03) * inv_det;
    r.m[13] = (a20 * b05 - a22 * b02 + a23 * b01) * inv_det;
    r.m[14] = (-a20 * b04 + a21 * b02 - a23 * b00) * inv_det;
    r.m[15] = (a20 * b03 - a21 * b01 + a22 * b00) * inv_det;
    return r;
}

// ─── quat ────────────────────────────────────────────────────────────────────
// Quaternion: x, y, z (imaginary), w (real). Identity = {0, 0, 0, 1}.

struct quat
{
    double x = 0.0, y = 0.0, z = 0.0, w = 1.0;

    constexpr quat() = default;
    constexpr quat(double x_, double y_, double z_, double w_) : x(x_), y(y_), z(z_), w(w_) {}

    constexpr bool operator==(quat b) const { return x == b.x && y == b.y && z == b.z && w == b.w; }
    constexpr bool operator!=(quat b) const { return !(*this == b); }
};

inline constexpr quat quat_identity()
{
    return {0.0, 0.0, 0.0, 1.0};
}

inline double quat_length(quat q)
{
    return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
}

inline quat quat_normalize(quat q)
{
    double len = quat_length(q);
    if (len < 1e-12)
        return quat_identity();
    double inv = 1.0 / len;
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

inline constexpr quat quat_conjugate(quat q)
{
    return {-q.x, -q.y, -q.z, q.w};
}

inline constexpr quat quat_mul(quat a, quat b)
{
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

inline quat quat_from_axis_angle(vec3 axis, double angle_rad)
{
    vec3  n    = vec3_normalize(axis);
    double half = angle_rad * 0.5;
    double s    = std::sin(half);
    return {n.x * s, n.y * s, n.z * s, std::cos(half)};
}

inline vec3 quat_rotate(quat q, vec3 v)
{
    // q * v * q^-1 (optimized)
    vec3 qv{q.x, q.y, q.z};
    vec3 t = vec3_cross(qv, v) * 2.0;
    return v + t * q.w + vec3_cross(qv, t);
}

inline mat4 quat_to_mat4(quat q)
{
    double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    double xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    double wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    mat4 m;
    m.m[0]  = 1.0 - 2.0 * (yy + zz);
    m.m[1]  = 2.0 * (xy + wz);
    m.m[2]  = 2.0 * (xz - wy);
    m.m[4]  = 2.0 * (xy - wz);
    m.m[5]  = 1.0 - 2.0 * (xx + zz);
    m.m[6]  = 2.0 * (yz + wx);
    m.m[8]  = 2.0 * (xz + wy);
    m.m[9]  = 2.0 * (yz - wx);
    m.m[10] = 1.0 - 2.0 * (xx + yy);
    m.m[15] = 1.0;
    return m;
}

inline quat quat_from_mat4(const mat4& m)
{
    double trace = m.m[0] + m.m[5] + m.m[10];
    quat  q;
    if (trace > 0.0)
    {
        double s = 0.5 / std::sqrt(trace + 1.0);
        q.w     = 0.25 / s;
        q.x     = (m.m[6] - m.m[9]) * s;
        q.y     = (m.m[8] - m.m[2]) * s;
        q.z     = (m.m[1] - m.m[4]) * s;
    }
    else if (m.m[0] > m.m[5] && m.m[0] > m.m[10])
    {
        double s = 2.0 * std::sqrt(1.0 + m.m[0] - m.m[5] - m.m[10]);
        q.w     = (m.m[6] - m.m[9]) / s;
        q.x     = 0.25 * s;
        q.y     = (m.m[4] + m.m[1]) / s;
        q.z     = (m.m[8] + m.m[2]) / s;
    }
    else if (m.m[5] > m.m[10])
    {
        double s = 2.0 * std::sqrt(1.0 + m.m[5] - m.m[0] - m.m[10]);
        q.w     = (m.m[8] - m.m[2]) / s;
        q.x     = (m.m[4] + m.m[1]) / s;
        q.y     = 0.25 * s;
        q.z     = (m.m[9] + m.m[6]) / s;
    }
    else
    {
        double s = 2.0 * std::sqrt(1.0 + m.m[10] - m.m[0] - m.m[5]);
        q.w     = (m.m[1] - m.m[4]) / s;
        q.x     = (m.m[8] + m.m[2]) / s;
        q.y     = (m.m[9] + m.m[6]) / s;
        q.z     = 0.25 * s;
    }
    return quat_normalize(q);
}

inline quat quat_slerp(quat a, quat b, float t)
{
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

    // If dot is negative, negate one quaternion to take the shorter path
    if (dot < 0.0f)
    {
        b   = {-b.x, -b.y, -b.z, -b.w};
        dot = -dot;
    }

    // If quaternions are very close, use linear interpolation
    if (dot > 0.9995f)
    {
        quat r = {
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t,
        };
        return quat_normalize(r);
    }

    float theta     = std::acos(dot);
    float sin_theta = std::sin(theta);
    float wa        = std::sin((1.0f - t) * theta) / sin_theta;
    float wb        = std::sin(t * theta) / sin_theta;

    return {
        a.x * wa + b.x * wb,
        a.y * wa + b.y * wb,
        a.z * wa + b.z * wb,
        a.w * wa + b.w * wb,
    };
}

// ─── Ray ─────────────────────────────────────────────────────────────────────

struct Ray
{
    vec3 origin;
    vec3 direction;
};

inline Ray unproject(float       screen_x,
                     float       screen_y,
                     const mat4& mvp_inv,
                     float       viewport_w,
                     float       viewport_h)
{
    // Screen to NDC (Vulkan: Y is flipped, Z range [0,1])
    float ndc_x = (2.0f * screen_x / viewport_w) - 1.0f;
    float ndc_y = (2.0f * screen_y / viewport_h) - 1.0f;

    vec4 near_pt = mat4_mul_vec4(mvp_inv, {ndc_x, ndc_y, 0.0f, 1.0f});
    vec4 far_pt  = mat4_mul_vec4(mvp_inv, {ndc_x, ndc_y, 1.0f, 1.0f});

    if (std::fabs(near_pt.w) < 1e-12f || std::fabs(far_pt.w) < 1e-12f)
        return {{0, 0, 0}, {0, 0, -1}};

    vec3 near3 = near_pt.xyz() / near_pt.w;
    vec3 far3  = far_pt.xyz() / far_pt.w;

    return {near3, vec3_normalize(far3 - near3)};
}

// ─── Utility ─────────────────────────────────────────────────────────────────

inline constexpr float deg_to_rad(float deg)
{
    return deg * 3.14159265358979323846f / 180.0f;
}
inline constexpr float rad_to_deg(float rad)
{
    return rad * 180.0f / 3.14159265358979323846f;
}

inline constexpr float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

}   // namespace spectra
