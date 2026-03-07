#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <spectra/math3d.hpp>

#ifdef SPECTRA_USE_ROS2
#include <sensor_msgs/msg/laser_scan.hpp>
#endif

namespace spectra::adapters::ros2
{

struct LaserScanPoint
{
    spectra::vec3 position{};
    float range{0.0f};
    float intensity{0.0f};
    bool has_intensity{false};
};

struct LaserScanFrame
{
    std::string topic;
    std::string frame_id;
    uint64_t stamp_ns{0};
    size_t point_count{0};
    std::vector<LaserScanPoint> points;
    spectra::vec3 min_bounds{};
    spectra::vec3 max_bounds{};
    spectra::vec3 centroid{};
    float min_range{0.0f};
    float max_range{0.0f};
    float average_range{0.0f};
    bool has_intensity{false};
    float min_intensity{0.0f};
    float max_intensity{0.0f};
    float average_intensity{0.0f};
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
    frame.points.reserve(message.ranges.size());

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
    double intensity_sum = 0.0;
    float observed_min_range = std::numeric_limits<float>::infinity();
    float observed_max_range = 0.0f;
    float observed_min_intensity = std::numeric_limits<float>::infinity();
    float observed_max_intensity = -std::numeric_limits<float>::infinity();
    bool observed_intensity = false;

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
        LaserScanPoint point;
        point.range = range;
        point.position = {
            static_cast<double>(range * std::cos(angle)),
            static_cast<double>(range * std::sin(angle)),
            0.0,
        };
        if (i < message.intensities.size() && std::isfinite(message.intensities[i]))
        {
            point.intensity = message.intensities[i];
            point.has_intensity = true;
            observed_intensity = true;
            intensity_sum += point.intensity;
            observed_min_intensity = std::min(observed_min_intensity, point.intensity);
            observed_max_intensity = std::max(observed_max_intensity, point.intensity);
        }

        frame.points.push_back(point);
        min_bounds = spectra::vec3_min(min_bounds, point.position);
        max_bounds = spectra::vec3_max(max_bounds, point.position);
        centroid_sum += point.position;
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
    frame.has_intensity = observed_intensity;
    if (observed_intensity)
    {
        frame.min_intensity = observed_min_intensity;
        frame.max_intensity = observed_max_intensity;
        frame.average_intensity =
            static_cast<float>(intensity_sum / static_cast<double>(frame.point_count));
    }
    return frame;
}
#endif

}   // namespace spectra::adapters::ros2
