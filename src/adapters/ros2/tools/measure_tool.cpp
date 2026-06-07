#include "tools/measure_tool.hpp"

#include <spectra/math3d.hpp>

namespace spectra::adapters::ros2
{

void MeasureTool::click_point(const spectra::vec3& world)
{
    if (!active_)
        return;

    if (click_count_ == 0)
    {
        first_       = world;
        click_count_ = 1;
        has_result_  = false;
        return;
    }

    second_      = world;
    click_count_ = 0;
    distance_    = static_cast<double>(spectra::vec3_length(first_ - second_));
    has_result_  = true;
}

}   // namespace spectra::adapters::ros2
