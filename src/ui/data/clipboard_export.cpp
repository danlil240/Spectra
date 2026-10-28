#include "clipboard_export.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <spectra/series.hpp>

namespace spectra
{

namespace
{

// Significant digits that show an absolute value (offset + stored float) to
// the precision the float offsets actually carry: `max_abs_stored` is the
// largest stored magnitude of the column, and float spacing there bounds the
// resolution of every value in it.  0 means "shortest round-trip form" (all
// stored values are 0, so each value is exactly the offset).
int digits_for_offset_column(double offset, float max_abs_stored)
{
    if (max_abs_stored == 0.0f)
        return 0;
    const double res =
        static_cast<double>(std::nextafter(max_abs_stored, std::numeric_limits<float>::max()))
        - static_cast<double>(max_abs_stored);
    const double mag    = std::abs(offset) + static_cast<double>(max_abs_stored);
    const int    digits = static_cast<int>(std::floor(std::log10(mag / res))) + 1;
    return std::clamp(digits, 6, 17);
}

float max_abs_finite(std::span<const float> v)
{
    float m = 0.0f;
    for (float f : v)
        if (std::isfinite(f))
            m = std::max(m, std::abs(f));
    return m;
}

// Plain float columns keep the historical 6 significant digits; columns with
// a double origin print offset + value in double with enough digits that
// e.g. 1e7 + 0.123 or a Unix timestamp is not rounded away.
void append_value(std::string& out, float stored, double offset, int digits)
{
    if (offset == 0.0)
        out += std::format("{:.6g}", stored);
    else if (digits == 0)
        out += std::format("{}", offset + static_cast<double>(stored));
    else
        out += std::format("{:.{}g}", offset + static_cast<double>(stored), digits);
}

}   // namespace

std::string series_to_tsv(const std::vector<const Series*>& series)
{
    if (series.empty())
        return {};

    std::string result;
    result.reserve(4096);

    // Collect typed pointers and max row count
    struct Col
    {
        const LineSeries*    line    = nullptr;
        const ScatterSeries* scatter = nullptr;
        std::string          label;
        size_t               count    = 0;
        double               x_off    = 0.0;
        double               y_off    = 0.0;
        int                  x_digits = 6;
        int                  y_digits = 6;
    };
    std::vector<Col> cols;
    size_t           max_rows = 0;

    for (const auto* s : series)
    {
        if (!s)
            continue;
        Col c;
        c.label = s->label().empty() ? "series" : s->label();
        if (auto* ls = dynamic_cast<const LineSeries*>(s))
        {
            c.line  = ls;
            c.count = ls->point_count();
        }
        else if (auto* ss = dynamic_cast<const ScatterSeries*>(s))
        {
            c.scatter = ss;
            c.count   = ss->point_count();
        }
        else
        {
            continue;   // 3D series not supported in TSV export yet
        }
        c.x_off       = s->x_offset();
        c.y_off       = s->y_offset();
        const auto xs = c.line ? c.line->x_data() : c.scatter->x_data();
        const auto ys = c.line ? c.line->y_data() : c.scatter->y_data();
        if (c.x_off != 0.0)
            c.x_digits = digits_for_offset_column(c.x_off, max_abs_finite(xs.first(c.count)));
        if (c.y_off != 0.0)
            c.y_digits = digits_for_offset_column(c.y_off, max_abs_finite(ys.first(c.count)));
        if (c.count > max_rows)
            max_rows = c.count;
        cols.push_back(std::move(c));
    }

    if (cols.empty())
        return {};

    // Header row
    for (size_t ci = 0; ci < cols.size(); ++ci)
    {
        if (ci > 0)
            result += '\t';
        result += cols[ci].label;
        result += "_x\t";
        result += cols[ci].label;
        result += "_y";
    }
    result += '\n';

    // Data rows
    for (size_t row = 0; row < max_rows; ++row)
    {
        for (size_t ci = 0; ci < cols.size(); ++ci)
        {
            if (ci > 0)
                result += '\t';

            const auto& col = cols[ci];
            if (row < col.count)
            {
                float x = 0.0f;
                float y = 0.0f;
                if (col.line)
                {
                    x = col.line->x_data()[row];
                    y = col.line->y_data()[row];
                }
                else if (col.scatter)
                {
                    x = col.scatter->x_data()[row];
                    y = col.scatter->y_data()[row];
                }
                append_value(result, x, col.x_off, col.x_digits);
                result += '\t';
                append_value(result, y, col.y_off, col.y_digits);
            }
            else
            {
                result += '\t';   // empty cells for shorter series
            }
        }
        result += '\n';
    }

    return result;
}

}   // namespace spectra
