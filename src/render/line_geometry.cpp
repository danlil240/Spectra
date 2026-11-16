#include "render/line_geometry.hpp"

#include <cmath>

namespace spectra
{

void build_line_dash_phase(std::span<const float> xy,
                           double                 px_per_unit_x,
                           double                 px_per_unit_y,
                           double                 period,
                           std::vector<float>&    out)
{
    const size_t n = xy.size() / 2;
    out.resize(n * 3);
    const bool wrap  = period > 0.0 && std::isfinite(period);
    double     phase = 0.0;
    bool       have  = false;   // previous point finite
    double     px = 0.0, py = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const float x  = xy[2 * i];
        const float y  = xy[2 * i + 1];
        const bool  ok = std::isfinite(x) && std::isfinite(y);
        if (!ok)
        {
            phase = 0.0;
            have  = false;
        }
        else if (have)
        {
            const double dx = (static_cast<double>(x) - px) * px_per_unit_x;
            const double dy = (static_cast<double>(y) - py) * px_per_unit_y;
            phase += std::sqrt(dx * dx + dy * dy);
            if (wrap)
                phase = std::fmod(phase, period);
        }
        else
        {
            phase = 0.0;
            have  = true;
        }
        if (ok)
        {
            px = x;
            py = y;
        }
        out[3 * i]     = x;
        out[3 * i + 1] = y;
        out[3 * i + 2] = static_cast<float>(phase);
    }
}

}   // namespace spectra
