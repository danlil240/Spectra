#pragma once

// GPS position adapter — converts ULog vehicle_gps_position or
// vehicle_global_position messages into GPS coordinate frames.

#include "../ulog_reader.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// GpsFrame — one GPS sample.
// ---------------------------------------------------------------------------

struct GpsFrame
{
    double  timestamp_sec{0.0};
    double  lat{0.0};   // degrees
    double  lon{0.0};   // degrees
    double  alt{0.0};   // meters (MSL)
    double  alt_ellipsoid{0.0};
    float   hdop{0.0f};
    float   vdop{0.0f};
    float   vel_m_s{0.0f};   // ground speed m/s
    uint8_t fix_type{0};     // 0=none, 2=2D, 3=3D, 4=DGPS, 5=RTK
    uint8_t satellites_used{0};
};

// ---------------------------------------------------------------------------
// Extract GPS time series from ULog data.
// ---------------------------------------------------------------------------

inline std::vector<GpsFrame> extract_gps(const ULogTimeSeries& ts)
{
    if (!ts.format)
        return {};

    std::vector<GpsFrame> frames;
    frames.reserve(ts.rows.size());

    // Find fields by name.
    auto find_field = [&](const std::string& name) -> const ULogField*
    {
        for (auto& f : ts.format->fields)
        {
            if (f.name == name)
                return &f;
        }
        return nullptr;
    };

    auto* f_lat   = find_field("lat");
    auto* f_lon   = find_field("lon");
    auto* f_alt   = find_field("alt");
    auto* f_alt_e = find_field("alt_ellipsoid");
    auto* f_hdop  = find_field("hdop");
    auto* f_vdop  = find_field("vdop");
    auto* f_vel   = find_field("vel_m_s");
    auto* f_fix   = find_field("fix_type");
    auto* f_sat   = find_field("satellites_used");

    for (auto& row : ts.rows)
    {
        GpsFrame gf;
        gf.timestamp_sec = static_cast<double>(row.timestamp_us) * 1e-6;

        // GPS coordinates may be int32 (1e-7 degrees) or double.
        if (f_lat)
        {
            if (f_lat->base_type == ULogFieldType::Int32)
                gf.lat = static_cast<double>(row.field_at<int32_t>(f_lat->offset)) * 1e-7;
            else if (f_lat->base_type == ULogFieldType::Double)
                gf.lat = row.field_at<double>(f_lat->offset);
        }
        if (f_lon)
        {
            if (f_lon->base_type == ULogFieldType::Int32)
                gf.lon = static_cast<double>(row.field_at<int32_t>(f_lon->offset)) * 1e-7;
            else if (f_lon->base_type == ULogFieldType::Double)
                gf.lon = row.field_at<double>(f_lon->offset);
        }
        if (f_alt)
        {
            if (f_alt->base_type == ULogFieldType::Int32)
                gf.alt = static_cast<double>(row.field_at<int32_t>(f_alt->offset)) * 1e-3;
            else if (f_alt->base_type == ULogFieldType::Float)
                gf.alt = row.field_at<float>(f_alt->offset);
        }
        if (f_alt_e)
            gf.alt_ellipsoid = row.field_at<float>(f_alt_e->offset);
        if (f_hdop)
            gf.hdop = row.field_at<float>(f_hdop->offset);
        if (f_vdop)
            gf.vdop = row.field_at<float>(f_vdop->offset);
        if (f_vel)
            gf.vel_m_s = row.field_at<float>(f_vel->offset);
        if (f_fix)
            gf.fix_type = row.field_at<uint8_t>(f_fix->offset);
        if (f_sat)
            gf.satellites_used = row.field_at<uint8_t>(f_sat->offset);

        frames.push_back(gf);
    }

    return frames;
}

}   // namespace spectra::adapters::px4
