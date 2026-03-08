#pragma once

// VehicleAttitude adapter — converts ULog vehicle_attitude messages into
// roll/pitch/yaw/quaternion frames for visualization.
//
// The vehicle_attitude ULog topic contains:
//   - timestamp (uint64_t)
//   - q[4] (float) — quaternion [w, x, y, z]
//   - delta_q_reset[4] (float)
//   - quat_reset_counter (uint8_t)

#include "../ulog_reader.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// AttitudeFrame — one attitude sample with Euler angles.
// ---------------------------------------------------------------------------

struct AttitudeFrame
{
    double timestamp_sec{0.0};   // seconds since boot
    float  roll{0.0f};          // radians
    float  pitch{0.0f};         // radians
    float  yaw{0.0f};           // radians
    float  q[4]{1, 0, 0, 0};   // quaternion [w, x, y, z]
};

// ---------------------------------------------------------------------------
// Extract attitude time series from ULog data.
// ---------------------------------------------------------------------------

inline std::vector<AttitudeFrame>
extract_attitude(const ULogTimeSeries& ts)
{
    if (!ts.format)
        return {};

    std::vector<AttitudeFrame> frames;
    frames.reserve(ts.rows.size());

    // Find q field.
    const ULogField* q_field = nullptr;
    for (auto& f : ts.format->fields)
    {
        if (f.name == "q" && f.array_size >= 4)
        {
            q_field = &f;
            break;
        }
    }

    for (auto& row : ts.rows)
    {
        AttitudeFrame af;
        af.timestamp_sec = static_cast<double>(row.timestamp_us) * 1e-6;

        if (q_field)
        {
            size_t off = q_field->offset;
            af.q[0] = row.field_at<float>(off);       // w
            af.q[1] = row.field_at<float>(off + 4);    // x
            af.q[2] = row.field_at<float>(off + 8);    // y
            af.q[3] = row.field_at<float>(off + 12);   // z

            // Quaternion to Euler (ZYX convention).
            float w = af.q[0], x = af.q[1], y = af.q[2], z = af.q[3];
            af.roll  = std::atan2(2.0f * (w * x + y * z),
                                   1.0f - 2.0f * (x * x + y * y));
            af.pitch = std::asin(std::clamp(2.0f * (w * y - z * x), -1.0f, 1.0f));
            af.yaw   = std::atan2(2.0f * (w * z + x * y),
                                   1.0f - 2.0f * (y * y + z * z));
        }

        frames.push_back(af);
    }

    return frames;
}

}   // namespace spectra::adapters::px4
