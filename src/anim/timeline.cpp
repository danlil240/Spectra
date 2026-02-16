#include <algorithm>
#include <cassert>
#include <plotix/timeline.hpp>

namespace plotix
{

namespace
{

// Lerp helper for float
float lerp_float(float a, float b, float t)
{
    return a + (b - a) * t;
}

// Lerp helper for Color
Color lerp_color(const Color& a, const Color& b, float t)
{
    return Color{lerp_float(a.r, b.r, t),
                 lerp_float(a.g, b.g, t),
                 lerp_float(a.b, b.b, t),
                 lerp_float(a.a, b.a, t)};
}

// Interpolate AnimValue — both must be the same type
AnimValue lerp_value(const AnimValue& a, const AnimValue& b, float t)
{
    if (std::holds_alternative<float>(a) && std::holds_alternative<float>(b))
    {
        return lerp_float(std::get<float>(a), std::get<float>(b), t);
    }
    if (std::holds_alternative<Color>(a) && std::holds_alternative<Color>(b))
    {
        return lerp_color(std::get<Color>(a), std::get<Color>(b), t);
    }
    // Type mismatch — return first value
    return a;
}

}  // anonymous namespace

Timeline& Timeline::add(float time, float value, EasingFn easing)
{
    keyframes_.push_back({time, AnimValue{value}, easing});
    // Keep sorted by time
    std::sort(keyframes_.begin(),
              keyframes_.end(),
              [](const KeyframeEntry& a, const KeyframeEntry& b) { return a.time < b.time; });
    return *this;
}

Timeline& Timeline::add(float time, const Color& value, EasingFn easing)
{
    keyframes_.push_back({time, AnimValue{value}, easing});
    std::sort(keyframes_.begin(),
              keyframes_.end(),
              [](const KeyframeEntry& a, const KeyframeEntry& b) { return a.time < b.time; });
    return *this;
}

AnimValue Timeline::evaluate(float t) const
{
    if (keyframes_.empty())
    {
        return 0.0f;
    }

    // Before first keyframe
    if (t <= keyframes_.front().time)
    {
        return keyframes_.front().value;
    }

    // After last keyframe
    if (t >= keyframes_.back().time)
    {
        return keyframes_.back().value;
    }

    // Find surrounding keyframes
    for (size_t i = 0; i + 1 < keyframes_.size(); ++i)
    {
        const auto& kf0 = keyframes_[i];
        const auto& kf1 = keyframes_[i + 1];

        if (t >= kf0.time && t <= kf1.time)
        {
            float segment_duration = kf1.time - kf0.time;
            if (segment_duration <= 0.0f)
            {
                return kf1.value;
            }

            float local_t = (t - kf0.time) / segment_duration;

            // Apply easing from the destination keyframe
            if (kf1.easing)
            {
                local_t = kf1.easing(local_t);
            }

            return lerp_value(kf0.value, kf1.value, local_t);
        }
    }

    // Fallback
    return keyframes_.back().value;
}

float Timeline::duration() const
{
    if (keyframes_.empty())
        return 0.0f;
    return keyframes_.back().time;
}

bool Timeline::empty() const
{
    return keyframes_.empty();
}

}  // namespace plotix
