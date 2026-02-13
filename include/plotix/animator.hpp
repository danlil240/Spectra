#pragma once

#include <plotix/fwd.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace plotix {

namespace ease {
    float linear(float t);
    float ease_in(float t);
    float ease_out(float t);
    float ease_in_out(float t);
    float bounce(float t);
    float elastic(float t);
} // namespace ease

using EasingFn = float(*)(float);

template <typename T>
struct Keyframe {
    float    time  = 0.0f;
    T        value {};
    EasingFn easing = ease::linear;
};

class Animator {
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

} // namespace plotix
