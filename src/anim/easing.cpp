#include <plotix/animator.hpp>

#include <cmath>

namespace plotix::ease {

namespace {
    constexpr float PI = 3.14159265358979323846f;
} // anonymous namespace

float linear(float t) {
    return t;
}

float ease_in(float t) {
    // Cubic ease-in
    return t * t * t;
}

float ease_out(float t) {
    // Cubic ease-out
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float ease_in_out(float t) {
    // Cubic ease-in-out
    if (t < 0.5f) {
        return 4.0f * t * t * t;
    } else {
        float u = -2.0f * t + 2.0f;
        return 1.0f - u * u * u / 2.0f;
    }
}

float bounce(float t) {
    // Bounce ease-out
    constexpr float n1 = 7.5625f;
    constexpr float d1 = 2.75f;

    if (t < 1.0f / d1) {
        return n1 * t * t;
    } else if (t < 2.0f / d1) {
        t -= 1.5f / d1;
        return n1 * t * t + 0.75f;
    } else if (t < 2.5f / d1) {
        t -= 2.25f / d1;
        return n1 * t * t + 0.9375f;
    } else {
        t -= 2.625f / d1;
        return n1 * t * t + 0.984375f;
    }
}

float elastic(float t) {
    // Elastic ease-out
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;

    constexpr float c4 = (2.0f * PI) / 3.0f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}

} // namespace plotix::ease
