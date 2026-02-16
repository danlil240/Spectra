#include <algorithm>
#include <plotix/animator.hpp>
#include <plotix/timeline.hpp>

namespace plotix
{

void Animator::add_timeline(std::shared_ptr<Timeline> tl)
{
    if (tl)
    {
        timelines_.push_back(std::move(tl));
    }
}

void Animator::remove_timeline(const std::shared_ptr<Timeline>& tl)
{
    timelines_.erase(std::remove(timelines_.begin(), timelines_.end(), tl), timelines_.end());
}

void Animator::evaluate(float time)
{
    if (paused_)
        return;

    for (auto& tl : timelines_)
    {
        if (tl && !tl->empty())
        {
            tl->evaluate(time);
        }
    }
}

void Animator::pause()
{
    paused_ = true;
}

void Animator::resume()
{
    paused_ = false;
}

void Animator::clear()
{
    timelines_.clear();
}

}  // namespace plotix
