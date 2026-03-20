#include <spectra/animator.hpp>
#include <spectra/logger.hpp>

namespace spectra
{

void Animator::evaluate(float /*time*/)
{
    if (paused_)
        return;
}

void Animator::pause()
{
    SPECTRA_LOG_TRACE("anim", "Animation paused");
    paused_ = true;
}

void Animator::resume()
{
    SPECTRA_LOG_TRACE("anim", "Animation resumed");
    paused_ = false;
}

void Animator::clear()
{
    SPECTRA_LOG_TRACE("anim", "Animation cleared");
    paused_ = false;
}

}   // namespace spectra
