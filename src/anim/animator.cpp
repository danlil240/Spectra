#include <spectra/animator.hpp>

namespace spectra
{

void Animator::evaluate(float /*time*/)
{
    if (paused_)
        return;
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
    paused_ = false;
}

}  // namespace spectra
