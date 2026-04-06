#pragma once

#include <string_view>

namespace spectra::adapters::ros2
{

// Slot-level axis behavior shared by runtime plotting and session persistence.
enum class AxisMode
{
    TimeSeries,
    CustomAxes,
};

inline constexpr const char* AXIS_SOURCE_TIME = "__time__";

inline constexpr std::string_view axis_mode_name(AxisMode mode)
{
    switch (mode)
    {
        case AxisMode::TimeSeries:
            return "time-series";
        case AxisMode::CustomAxes:
            return "custom-axes";
    }
    return "time-series";
}

inline constexpr AxisMode axis_mode_from_name(std::string_view text)
{
    if (text == "custom-axes")
        return AxisMode::CustomAxes;
    return AxisMode::TimeSeries;
}

}   // namespace spectra::adapters::ros2
