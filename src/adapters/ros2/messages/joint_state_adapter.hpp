#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#ifdef SPECTRA_USE_ROS2
    #include <sensor_msgs/msg/joint_state.hpp>
#endif

namespace spectra::adapters::ros2
{

/// Parsed joint state: maps joint names to their current positions.
struct JointStateFrame
{
    std::unordered_map<std::string, double> positions;
    uint64_t                                stamp_ns{0};
};

#ifdef SPECTRA_USE_ROS2
/// Convert a sensor_msgs/JointState message to a JointStateFrame.
inline JointStateFrame adapt_joint_state(const sensor_msgs::msg::JointState& msg)
{
    JointStateFrame frame;
    frame.stamp_ns = static_cast<uint64_t>(msg.header.stamp.sec) * 1'000'000'000ULL
                     + static_cast<uint64_t>(msg.header.stamp.nanosec);

    const size_t count = std::min(msg.name.size(), msg.position.size());
    for (size_t i = 0; i < count; ++i)
        frame.positions[msg.name[i]] = msg.position[i];

    return frame;
}
#endif

}   // namespace spectra::adapters::ros2
