// field_path.hpp — Field-path fan-out: one message → many topics.
//
// A decoded message is a flat list of (path, value) leaves, e.g.
//   {"imu": {"accel": {"x": 0.1, "y": 9.8}}, "t": 12.5}
// flattens to imu/accel/x = 0.1, imu/accel/y = 9.8, t = 12.5.  fan_out_message()
// joins each path to a prefix ("udp/imu/accel/x") and gives every sample the
// same x: the message's own top-level "t" / "timestamp" field when present,
// otherwise the receive time supplied by the bridge.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "topic_bridge.hpp"

namespace spectra::daemon
{

// One numeric leaf of a decoded message.
struct FlatField
{
    std::string path;   // '/'-separated, e.g. "imu/accel/x" or "joints/3"
    double      value = 0.0;
};

// Longest topic name a bridge will create.  Longer names are dropped.
inline constexpr size_t MAX_BRIDGE_TOPIC_NAME = 200;

// Join prefix and path with '/', trimming stray separators and replacing
// control characters with '_'.  Returns an empty string when the result is
// empty or longer than MAX_BRIDGE_TOPIC_NAME.
std::string join_topic_path(std::string_view prefix, std::string_view path);

// True for the top-level field names that carry the message timestamp.
bool is_timestamp_field(std::string_view path);

// Append one TopicFieldSample per non-timestamp field to `out`, all sharing
// the message timestamp (or `default_time`).  Fields whose topic name is
// invalid are skipped and counted in *skipped (if non-null).
void fan_out_message(const std::vector<FlatField>&  fields,
                     std::string_view               prefix,
                     double                         default_time,
                     std::vector<TopicFieldSample>& out,
                     size_t*                        skipped = nullptr);

}   // namespace spectra::daemon
