#include "data/line_geometry.hpp"

namespace spectra::data
{

void expand_steps(std::span<const float> x,
                  std::span<const float> y,
                  DrawStyle              style,
                  std::vector<float>&    out_x,
                  std::vector<float>&    out_y)
{
    const size_t n = std::min(x.size(), y.size());
    if (n == 0)
        return;
    const size_t total = step_vertex_count(n, style);
    out_x.reserve(out_x.size() + total);
    out_y.reserve(out_y.size() + total);

    for (size_t i = 0; i < n; ++i)
    {
        out_x.push_back(x[i]);
        out_y.push_back(y[i]);
        if (i + 1 == n)
            break;
        switch (style)
        {
            case DrawStyle::StepsPre:
                out_x.push_back(x[i]);
                out_y.push_back(y[i + 1]);
                break;
            case DrawStyle::StepsPost:
                out_x.push_back(x[i + 1]);
                out_y.push_back(y[i]);
                break;
            case DrawStyle::StepsMid:
            {
                const float mid = x[i] + (x[i + 1] - x[i]) * 0.5f;
                out_x.push_back(mid);
                out_y.push_back(y[i]);
                out_x.push_back(mid);
                out_y.push_back(y[i + 1]);
                break;
            }
            case DrawStyle::Default:
                break;
        }
    }
}

void stairs_points(std::span<const float> values,
                   std::span<const float> edges,
                   std::vector<float>&    out_x,
                   std::vector<float>&    out_y)
{
    out_x.clear();
    out_y.clear();
    if (values.empty() || edges.size() != values.size() + 1)
        return;
    out_x.assign(edges.begin(), edges.end());
    out_y.assign(values.begin(), values.end());
    out_y.push_back(values.back());
}

}   // namespace spectra::data
