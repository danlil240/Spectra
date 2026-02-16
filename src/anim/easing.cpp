#include <algorithm>
#include <cmath>
#include <plotix/animator.hpp>

namespace plotix::ease
{

namespace
{
constexpr float PI = 3.14159265358979323846f;
}  // anonymous namespace

float linear(float t)
{
    return t;
}

float ease_in(float t)
{
    // Cubic ease-in
    return t * t * t;
}

float ease_out(float t)
{
    // Cubic ease-out
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float ease_in_out(float t)
{
    // Cubic ease-in-out
    if (t < 0.5f)
    {
        return 4.0f * t * t * t;
    }
    else
    {
        float u = -2.0f * t + 2.0f;
        return 1.0f - u * u * u / 2.0f;
    }
}

float bounce(float t)
{
    // Bounce ease-out
    constexpr float n1 = 7.5625f;
    constexpr float d1 = 2.75f;

    if (t < 1.0f / d1)
    {
        return n1 * t * t;
    }
    else if (t < 2.0f / d1)
    {
        t -= 1.5f / d1;
        return n1 * t * t + 0.75f;
    }
    else if (t < 2.5f / d1)
    {
        t -= 2.25f / d1;
        return n1 * t * t + 0.9375f;
    }
    else
    {
        t -= 2.625f / d1;
        return n1 * t * t + 0.984375f;
    }
}

float elastic(float t)
{
    // Elastic ease-out
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;

    constexpr float c4 = (2.0f * PI) / 3.0f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}

float spring(float t)
{
    // Damped spring: overshoots slightly then settles
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;

    constexpr float damping = 6.0f;
    constexpr float freq = 4.5f;
    return 1.0f - std::exp(-damping * t) * std::cos(freq * PI * t);
}

float decelerate(float t)
{
    // Quadratic deceleration — ideal for inertial pan
    return 1.0f - (1.0f - t) * (1.0f - t);
}

float CubicBezier::operator()(float t) const
{
    // Solve cubic bezier using Newton-Raphson iteration
    // Find parameter u such that bezier_x(u) == t, then return bezier_y(u)
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;

    float u = t;  // initial guess
    for (int i = 0; i < 8; ++i)
    {
        // bezier_x(u) = 3*(1-u)^2*u*x1 + 3*(1-u)*u^2*x2 + u^3
        float u2 = u * u;
        float u3 = u2 * u;
        float inv = 1.0f - u;
        float inv2 = inv * inv;

        float bx = 3.0f * inv2 * u * x1 + 3.0f * inv * u2 * x2 + u3;
        float dx = 3.0f * inv2 * x1 + 6.0f * inv * u * (x2 - x1) + 3.0f * u2 * (1.0f - x2);

        if (std::abs(dx) < 1e-7f)
            break;
        u -= (bx - t) / dx;
        u = std::clamp(u, 0.0f, 1.0f);
    }

    // Evaluate y at parameter u
    float inv = 1.0f - u;
    float inv2 = inv * inv;
    float u2 = u * u;
    return 3.0f * inv2 * u * y1 + 3.0f * inv * u2 * y2 + u2 * u;
}

}  // namespace plotix::ease
