#include "data/line_density.hpp"

#include <algorithm>
#include <cmath>

namespace spectra::data
{

void LineDensityGrid::reset(uint32_t cols, uint32_t rows)
{
    cols_ = cols;
    rows_ = rows;
    diff_.assign(static_cast<size_t>(cols) * (static_cast<size_t>(rows) + 1), 0.0f);
}

void LineDensityGrid::add_span(uint32_t col, double lo, double hi)
{
    const double rows = static_cast<double>(rows_);
    lo                = std::clamp(lo, 0.0, rows);
    hi                = std::clamp(hi, 0.0, rows);
    if (!(hi > lo))
        return;
    float* d = diff_.data() + static_cast<size_t>(col) * (rows_ + 1);
    // Rows r0..r1 are touched; the last one only up to hi.
    const auto r0 = static_cast<uint32_t>(std::floor(lo));
    auto       r1 = static_cast<uint32_t>(std::floor(hi));
    if (r1 >= rows_)
        r1 = rows_ - 1;
    auto add = [d](uint32_t first, uint32_t last, double v)
    {
        d[first] += static_cast<float>(v);
        d[last + 1] -= static_cast<float>(v);
    };
    if (r0 == r1)
    {
        add(r0, r0, hi - lo);
        return;
    }
    add(r0, r0, static_cast<double>(r0) + 1.0 - lo);
    if (r1 > r0 + 1)
        add(r0 + 1, r1 - 1, 1.0);
    add(r1, r1, std::min(hi - static_cast<double>(r1), 1.0));
}

void LineDensityGrid::add_segment(double x0, double y0, double x1, double y1, double half_width)
{
    if (cols_ == 0 || rows_ == 0)
        return;
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1))
        return;
    if (x1 < x0)
    {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    const double cols = static_cast<double>(cols_);
    if (x1 < 0.0 || x0 >= cols)
        return;
    const double hw = std::max(half_width, 0.0);
    const double dx = x1 - x0;
    const double c0 = std::max(std::floor(x0), 0.0);
    const double c1 = std::min(std::floor(x1), cols - 1.0);
    if (!(dx > 0.0))
    {
        add_span(static_cast<uint32_t>(c0), std::min(y0, y1) - hw, std::max(y0, y1) + hw);
        return;
    }
    const double slope = (y1 - y0) / dx;
    for (double c = c0; c <= c1; c += 1.0)
    {
        const double xa = std::max(x0, c);
        const double xb = std::min(x1, c + 1.0);
        const double ya = y0 + slope * (xa - x0);
        const double yb = y0 + slope * (xb - x0);
        add_span(static_cast<uint32_t>(c), std::min(ya, yb) - hw, std::max(ya, yb) + hw);
    }
}

float LineDensityGrid::coverage(uint32_t col, uint32_t row) const
{
    if (col >= cols_ || row >= rows_)
        return 0.0f;
    const float* d   = diff_.data() + static_cast<size_t>(col) * (rows_ + 1);
    float        sum = 0.0f;
    for (uint32_t r = 0; r <= row; ++r)
        sum += d[r];
    return sum;
}

void LineDensityGrid::to_rgba(float                 r,
                              float                 g,
                              float                 b,
                              float                 alpha,
                              std::vector<uint8_t>& out) const
{
    out.assign(static_cast<size_t>(cols_) * rows_ * 4, 0);
    auto to8 = [](float v)
    { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    const uint8_t r8 = to8(r);
    const uint8_t g8 = to8(g);
    const uint8_t b8 = to8(b);
    alpha            = std::clamp(alpha, 0.0f, 1.0f);
    // ln(1 - a): the alpha of n overlaps is 1 - exp(n ln(1 - a)).
    const double log_t = alpha < 1.0f ? std::log1p(-static_cast<double>(alpha)) : 0.0;
    for (uint32_t c = 0; c < cols_; ++c)
    {
        const float* d   = diff_.data() + static_cast<size_t>(c) * (rows_ + 1);
        double       sum = 0.0;
        for (uint32_t row = 0; row < rows_; ++row)
        {
            sum += d[row];
            if (sum <= 1e-6)
                continue;
            const double a  = alpha < 1.0f ? -std::expm1(sum * log_t) : std::min(sum, 1.0);
            uint8_t*     px = out.data() + (static_cast<size_t>(row) * cols_ + c) * 4;
            px[0]           = r8;
            px[1]           = g8;
            px[2]           = b8;
            px[3]           = to8(static_cast<float>(a));
        }
    }
}

}   // namespace spectra::data
