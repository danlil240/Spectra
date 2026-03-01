#include "clipboard_export.hpp"

#include <cstdio>
#include <spectra/series.hpp>

namespace spectra
{

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
        size_t               count = 0;
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
    char buf[64];
    for (size_t row = 0; row < max_rows; ++row)
    {
        for (size_t ci = 0; ci < cols.size(); ++ci)
        {
            if (ci > 0)
                result += '\t';

            const auto& col = cols[ci];
            if (row < col.count)
            {
                float x = 0.0f, y = 0.0f;
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
                std::snprintf(buf, sizeof(buf), "%.6g", x);
                result += buf;
                result += '\t';
                std::snprintf(buf, sizeof(buf), "%.6g", y);
                result += buf;
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
