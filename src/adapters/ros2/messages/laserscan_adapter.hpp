#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include <spectra/math3d.hpp>

#ifdef SPECTRA_USE_ROS2
#include <sensor_msgs/msg/laser_scan.hpp>
#endif

namespace spectra::adapters::ros2
{

struct LaserScanFrame
{
    std::string topic;
    std::string frame_id;
    uint64_t stamp_ns{0};
    size_t point_count{0};
    spectra::vec3 min_bounds{};
    spectra::vec3 max_bounds{};
    spectra::vec3 centroid{};
    float min_range{0.0f};
    float max_range{0.0f};
    float average_range{0.0f};
    bool has_intensity{false};
};

#ifdef SPECTRA_USE_ROS2
inline std::optional<LaserScanFrame> adapt_laserscan_message(
    const sensor_msgs::msg::LaserScan& message,
    const std::string&                 topic,
    float                              min_range_filter = 0.0f,
    float                              max_range_filter = std::numeric_limits<float>::infinity())
{
    LaserScanFrame frame;
    frame.topic = topic;
    frame.frame_id = message.header.frame_id;
    frame.stamp_ns = static_cast<uint64_t>(message.header.stamp.sec) * 1'000'000'000ULL
                   + static_cast<uint64_t>(message.header.stamp.nanosec);
    frame.has_intensity = !message.intensities.empty();

    spectra::vec3 min_bounds{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    spectra::vec3 max_bounds{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    spectra::vec3 centroid_sum{};
    double range_sum = 0.0;
    float observed_min_range = std::numeric_limits<float>::infinity();
    float observed_max_range = 0.0f;

    for (size_t i = 0; i < message.ranges.size(); ++i)
    {
        const float range = message.ranges[i];
        if (!std::isfinite(range))
            continue;
        if (range < message.range_min || range > message.range_max)
            continue;
        if (range < min_range_filter || range > max_range_filter)
            continue;

        const float angle = message.angle_min + static_cast<float>(i) * message.angle_increment;
        const spectra::vec3 point{
            static_cast<double>(range * std::cos(angle)),
            static_cast<double>(range * std::sin(angle)),
            0.0,
        };
        min_bounds = spectra::vec3_min(min_bounds, point);
        max_bounds = spectra::vec3_max(max_bounds, point);
        centroid_sum += point;
        range_sum += range;
        observed_min_range = std::min(observed_min_range, range);
        observed_max_range = std::max(observed_max_range, range);
        ++frame.point_count;
    }

    if (frame.point_count == 0)
        return std::nullopt;

    frame.min_bounds = min_bounds;
    frame.max_bounds = max_bounds;
    frame.centroid = centroid_sum / static_cast<double>(frame.point_count);
    frame.min_range = observed_min_range;
    frame.max_range = observed_max_range;
    frame.average_range = static_cast<float>(range_sum / static_cast<double>(frame.point_count));
    return frame;
}
#endif

}   // namespace spectra::adapters::ros2
