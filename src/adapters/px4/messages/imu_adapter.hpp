#pragma once

// IMU/sensor adapter — converts ULog sensor_combined or sensor_accel/sensor_gyro
// messages into IMU frames for visualization.

#include "../ulog_reader.hpp"

#include <string>
#include <vector>

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// ImuFrame — one IMU sample.
// ---------------------------------------------------------------------------

struct ImuFrame
{
    double timestamp_sec{0.0};
    float  accel_x{0.0f};    // m/s²
    float  accel_y{0.0f};
    float  accel_z{0.0f};
    float  gyro_x{0.0f};     // rad/s
    float  gyro_y{0.0f};
    float  gyro_z{0.0f};
    float  mag_x{0.0f};      // Gauss
    float  mag_y{0.0f};
    float  mag_z{0.0f};
};

// ---------------------------------------------------------------------------
// Extract IMU time series from ULog sensor_combined data.
// ---------------------------------------------------------------------------

inline std::vector<ImuFrame>
extract_imu(const ULogTimeSeries& ts)
{
    if (!ts.format)
        return {};

    std::vector<ImuFrame> frames;
    frames.reserve(ts.rows.size());

    auto find_field = [&](const std::string& name) -> const ULogField*
    {
        for (auto& f : ts.format->fields)
        {
            if (f.name == name)
                return &f;
        }
        return nullptr;
    };

    // sensor_combined has gyro_rad[3] and accelerometer_m_s2[3].
    auto* f_gyro  = find_field("gyro_rad");
    auto* f_accel = find_field("accelerometer_m_s2");

    // sensor_accel has x, y, z.
    auto* f_ax = find_field("x");
    auto* f_ay = find_field("y");
    auto* f_az = find_field("z");

    for (auto& row : ts.rows)
    {
        ImuFrame frame;
        frame.timestamp_sec = static_cast<double>(row.timestamp_us) * 1e-6;

        if (f_accel && f_accel->array_size >= 3)
        {
            frame.accel_x = row.field_at<float>(f_accel->offset);
            frame.accel_y = row.field_at<float>(f_accel->offset + 4);
            frame.accel_z = row.field_at<float>(f_accel->offset + 8);
        }
        else if (f_ax && f_ay && f_az)
        {
            frame.accel_x = row.field_at<float>(f_ax->offset);
            frame.accel_y = row.field_at<float>(f_ay->offset);
            frame.accel_z = row.field_at<float>(f_az->offset);
        }

        if (f_gyro && f_gyro->array_size >= 3)
        {
            frame.gyro_x = row.field_at<float>(f_gyro->offset);
            frame.gyro_y = row.field_at<float>(f_gyro->offset + 4);
            frame.gyro_z = row.field_at<float>(f_gyro->offset + 8);
        }

        frames.push_back(frame);
    }

    return frames;
}

}   // namespace spectra::adapters::px4
