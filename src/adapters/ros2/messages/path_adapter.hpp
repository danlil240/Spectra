#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <spectra/math3d.hpp>

#ifdef SPECTRA_USE_ROS2
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <nav_msgs/msg/path.hpp>
#endif

namespace spectra::adapters::ros2
{

struct PathFrame
{
    std::string topic;
    std::string frame_id;
    uint64_t stamp_ns{0};
    size_t pose_count{0};
    std::vector<spectra::vec3> points;
    spectra::vec3 min_bounds{};
    spectra::vec3 max_bounds{};
    spectra::vec3 centroid{};
    spectra::vec3 start_point{};
    spectra::vec3 end_point{};
    double path_length_m{0.0};
};

struct PoseFrame
{
    std::string topic;
    std::string frame_id;
    uint64_t stamp_ns{0};
    spectra::Transform pose{};
};

#ifdef SPECTRA_USE_ROS2
namespace path_detail
{

inline uint64_t stamp_to_ns(const builtin_interfaces::msg::Time& stamp)
{
    return static_cast<uint64_t>(stamp.sec) * 1'000'000'000ULL
         + static_cast<uint64_t>(stamp.nanosec);
}

inline spectra::vec3 position_to_vec3(const geometry_msgs::msg::Point& point)
{
    return {point.x, point.y, point.z};
}

inline spectra::quat orientation_to_quat(const geometry_msgs::msg::Quaternion& orientation)
{
    return {
        static_cast<float>(orientation.x),
        static_cast<float>(orientation.y),
        static_cast<float>(orientation.z),
        static_cast<float>(orientation.w),
    };
}

}   // namespace path_detail

inline std::optional<PathFrame> adapt_path_message(const nav_msgs::msg::Path& message,
                                                   const std::string&         topic)
{
    if (message.poses.empty())
        return std::nullopt;

    PathFrame frame;
    frame.topic = topic;
    frame.frame_id = message.header.frame_id;
    frame.stamp_ns = path_detail::stamp_to_ns(message.header.stamp);
    frame.pose_count = message.poses.size();

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
    std::optional<spectra::vec3> previous_point;

    for (size_t i = 0; i < message.poses.size(); ++i)
    {
        const spectra::vec3 point = path_detail::position_to_vec3(message.poses[i].pose.position);
        frame.points.push_back(point);
        if (i == 0)
            frame.start_point = point;
        frame.end_point = point;
        min_bounds = spectra::vec3_min(min_bounds, point);
        max_bounds = spectra::vec3_max(max_bounds, point);
        centroid_sum += point;
        if (previous_point.has_value())
            frame.path_length_m += spectra::vec3_length(point - *previous_point);
        previous_point = point;
    }

    frame.min_bounds = min_bounds;
    frame.max_bounds = max_bounds;
    frame.centroid = centroid_sum / static_cast<double>(message.poses.size());
    return frame;
}

inline std::optional<PoseFrame> adapt_pose_stamped_message(
    const geometry_msgs::msg::PoseStamped& message,
    const std::string&                     topic)
{
    PoseFrame frame;
    frame.topic = topic;
    frame.frame_id = message.header.frame_id;
    frame.stamp_ns = path_detail::stamp_to_ns(message.header.stamp);
    frame.pose.translation = path_detail::position_to_vec3(message.pose.position);
    frame.pose.rotation = path_detail::orientation_to_quat(message.pose.orientation);
    return frame;
}
#endif

}   // namespace spectra::adapters::ros2
