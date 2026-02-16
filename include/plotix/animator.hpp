#pragma once

#include <functional>
#include <memory>
#include <plotix/fwd.hpp>
#include <vector>

namespace plotix
{

namespace ease
{
float linear(float t);
float ease_in(float t);
float ease_out(float t);
float ease_in_out(float t);
float bounce(float t);
float elastic(float t);
float spring(float t);
float decelerate(float t);

// Cubic-bezier easing factory (returns a stateless function object)
struct CubicBezier
{
    float x1, y1, x2, y2;
    float operator()(float t) const;
};

// Common presets
inline constexpr CubicBezier ease_out_cubic{0.215f, 0.61f, 0.355f, 1.0f};
inline constexpr CubicBezier ease_out_quart{0.165f, 0.84f, 0.44f, 1.0f};
inline constexpr CubicBezier ease_in_out_cubic{0.645f, 0.045f, 0.355f, 1.0f};
}  // namespace ease

using EasingFn = float (*)(float);

template <typename T>
struct Keyframe
{
    float time = 0.0f;
    T value{};
    EasingFn easing = ease::linear;
};

class Animator
{
   public:
    Animator() = default;

    void add_timeline(std::shared_ptr<Timeline> tl);
    void remove_timeline(const std::shared_ptr<Timeline>& tl);

    void evaluate(float time);

    void pause();
    void resume();
    bool is_paused() const { return paused_; }

    void clear();

   private:
    std::vector<std::shared_ptr<Timeline>> timelines_;
    bool paused_ = false;
};

}  // namespace plotix
