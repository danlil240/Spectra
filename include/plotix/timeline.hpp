#pragma once

#include <plotix/animator.hpp>
#include <plotix/color.hpp>
#include <variant>
#include <vector>

namespace plotix
{

using AnimValue = std::variant<float, Color>;

class Timeline
{
   public:
    Timeline() = default;

    // Add keyframes
    Timeline& add(float time, float value, EasingFn easing = ease::linear);
    Timeline& add(float time, const Color& value, EasingFn easing = ease::linear);

    // Evaluate at time t — returns interpolated value
    AnimValue evaluate(float t) const;

    // Duration of the timeline (time of last keyframe)
    float duration() const;

    bool empty() const;

   private:
    struct KeyframeEntry
    {
        float time;
        AnimValue value;
        EasingFn easing;
    };

    std::vector<KeyframeEntry> keyframes_;
};

}  // namespace plotix
