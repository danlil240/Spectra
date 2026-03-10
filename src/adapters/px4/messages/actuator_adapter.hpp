#pragma once

// Actuator output adapter — converts ULog actuator_outputs messages
// into actuator frames for visualization (motor/servo outputs).

#include "../ulog_reader.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// ActuatorFrame — one actuator output sample.
// ---------------------------------------------------------------------------

struct ActuatorFrame
{
    double timestamp_sec{0.0};
    float  output[16]{};   // up to 16 output channels
    int    noutputs{0};
};

// ---------------------------------------------------------------------------
// Extract actuator time series from ULog data.
// ---------------------------------------------------------------------------

inline std::vector<ActuatorFrame> extract_actuator_outputs(const ULogTimeSeries& ts)
{
    if (!ts.format)
        return {};

    std::vector<ActuatorFrame> frames;
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

    auto* f_noutputs = find_field("noutputs");
    auto* f_output   = find_field("output");

    for (auto& row : ts.rows)
    {
        ActuatorFrame af;
        af.timestamp_sec = static_cast<double>(row.timestamp_us) * 1e-6;

        if (f_noutputs)
            af.noutputs = static_cast<int>(row.field_at<uint32_t>(f_noutputs->offset));
        else if (f_output)
            af.noutputs = std::min(f_output->array_size, 16);

        if (f_output)
        {
            int count = std::min(af.noutputs, std::min(f_output->array_size, 16));
            for (int i = 0; i < count; ++i)
                af.output[i] = row.field_at<float>(f_output->offset + static_cast<size_t>(i) * 4);
        }

        frames.push_back(af);
    }

    return frames;
}

}   // namespace spectra::adapters::px4
