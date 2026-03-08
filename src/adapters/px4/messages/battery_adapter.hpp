#pragma once

// Battery status adapter — converts ULog battery_status messages
// into battery frames for visualization.

#include "../ulog_reader.hpp"

#include <string>
#include <vector>

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// BatteryFrame — one battery status sample.
// ---------------------------------------------------------------------------

struct BatteryFrame
{
    double timestamp_sec{0.0};
    float  voltage_v{0.0f};         // total battery voltage (V)
    float  current_a{0.0f};         // battery current (A)
    float  discharged_mah{0.0f};    // discharged capacity (mAh)
    float  remaining{0.0f};         // remaining capacity [0..1]
    float  temperature{0.0f};       // degrees Celsius
    uint8_t cell_count{0};
    bool    connected{false};
};

// ---------------------------------------------------------------------------
// Extract battery time series from ULog data.
// ---------------------------------------------------------------------------

inline std::vector<BatteryFrame>
extract_battery(const ULogTimeSeries& ts)
{
    if (!ts.format)
        return {};

    std::vector<BatteryFrame> frames;
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

    auto* f_voltage     = find_field("voltage_v");
    auto* f_current     = find_field("current_a");
    auto* f_discharged  = find_field("discharged_mah");
    auto* f_remaining   = find_field("remaining");
    auto* f_temperature = find_field("temperature");
    auto* f_cell_count  = find_field("cell_count");
    auto* f_connected   = find_field("connected");

    // Fallback field names used in older PX4 versions.
    if (!f_voltage)
        f_voltage = find_field("voltage_filtered_v");
    if (!f_current)
        f_current = find_field("current_filtered_a");

    for (auto& row : ts.rows)
    {
        BatteryFrame bf;
        bf.timestamp_sec = static_cast<double>(row.timestamp_us) * 1e-6;

        if (f_voltage)
            bf.voltage_v = row.field_at<float>(f_voltage->offset);
        if (f_current)
            bf.current_a = row.field_at<float>(f_current->offset);
        if (f_discharged)
            bf.discharged_mah = row.field_at<float>(f_discharged->offset);
        if (f_remaining)
            bf.remaining = row.field_at<float>(f_remaining->offset);
        if (f_temperature)
            bf.temperature = row.field_at<float>(f_temperature->offset);
        if (f_cell_count)
            bf.cell_count = row.field_at<uint8_t>(f_cell_count->offset);
        if (f_connected)
            bf.connected = row.field_at<uint8_t>(f_connected->offset) != 0;

        frames.push_back(bf);
    }

    return frames;
}

}   // namespace spectra::adapters::px4
