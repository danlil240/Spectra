#include "layout.hpp"

#include <algorithm>

namespace spectra
{

std::vector<Rect> compute_subplot_layout(float          figure_width,
                                         float          figure_height,
                                         int            rows,
                                         int            cols,
                                         const Margins& margins,
                                         float          min_subplot_height,
                                         float*         actual_content_height)
{
    return compute_subplot_layout(figure_width,
                                  figure_height,
                                  rows,
                                  cols,
                                  margins,
                                  0.0f,
                                  0.0f,
                                  min_subplot_height,
                                  actual_content_height);
}

std::vector<Rect> compute_subplot_layout(float          figure_width,
                                         float          figure_height,
                                         int            rows,
                                         int            cols,
                                         const Margins& margins,
                                         float          origin_x,
                                         float          origin_y,
                                         float          min_subplot_height,
                                         float*         actual_content_height)
{
    std::vector<Rect> rects;
    rects.reserve(static_cast<size_t>(rows) * static_cast<size_t>(cols));

    // Total available space for the grid of subplots
    // Each cell gets an equal share of the figure, then margins are applied inside each cell.
    float cell_width  = figure_width / static_cast<float>(cols);
    float cell_height = figure_height / static_cast<float>(rows);

    // Enforce minimum subplot height: the plot area inside a cell must be
    // at least min_subplot_height pixels tall.  If the natural cell height
    // is too small, expand it (the total content may exceed figure_height).
    if (min_subplot_height > 0.0f)
    {
        float min_cell_height = min_subplot_height + margins.top + margins.bottom;
        cell_height           = std::max(cell_height, min_cell_height);
    }

    float total_height = cell_height * static_cast<float>(rows);
    if (actual_content_height)
        *actual_content_height = total_height;

    // Row 0 is the top row. subplot(rows, cols, index) uses 1-based index,
    // row-major order (index 1 = top-left). We store row-major here.
    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            float cell_x = static_cast<float>(c) * cell_width;
            // Row 0 at top: y increases downward in screen coords
            float cell_y = static_cast<float>(r) * cell_height;

            Rect plot_area;
            plot_area.x = origin_x + cell_x + margins.left;
            plot_area.y = origin_y + cell_y + margins.top;
            plot_area.w = cell_width - margins.left - margins.right;
            plot_area.h = cell_height - margins.top - margins.bottom;

            // Clamp to non-negative dimensions
            if (plot_area.w < 0.0f)
                plot_area.w = 0.0f;
            if (plot_area.h < 0.0f)
                plot_area.h = 0.0f;

            rects.push_back(plot_area);
        }
    }

    return rects;
}

std::vector<Rect> compute_grid_layout(float              width,
                                      float              height,
                                      int                rows,
                                      int                cols,
                                      const GridSpacing& spacing,
                                      float              origin_x,
                                      float              origin_y,
                                      float              min_subplot_height,
                                      float*             actual_content_height)
{
    rows     = std::max(1, rows);
    cols     = std::max(1, cols);
    auto gap = [](const std::vector<float>& gaps, int i)
    { return i >= 0 && static_cast<size_t>(i) < gaps.size() ? std::max(0.0f, gaps[i]) : 0.0f; };

    float sum_col_gaps = 0.0f;
    for (int c = 0; c + 1 < cols; ++c)
        sum_col_gaps += gap(spacing.col_gaps, c);
    float sum_row_gaps = 0.0f;
    for (int r = 0; r + 1 < rows; ++r)
        sum_row_gaps += gap(spacing.row_gaps, r);

    const Margins& m = spacing.outer;
    const float    plot_w =
        std::max(0.0f, (width - m.left - m.right - sum_col_gaps) / static_cast<float>(cols));
    float plot_h =
        std::max(0.0f, (height - m.top - m.bottom - sum_row_gaps) / static_cast<float>(rows));
    if (min_subplot_height > 0.0f)
        plot_h = std::max(plot_h, min_subplot_height);

    if (actual_content_height)
        *actual_content_height =
            std::max(height, m.top + m.bottom + sum_row_gaps + plot_h * static_cast<float>(rows));

    std::vector<Rect> rects;
    rects.reserve(static_cast<size_t>(rows) * static_cast<size_t>(cols));
    float y = origin_y + m.top;
    for (int r = 0; r < rows; ++r)
    {
        float x = origin_x + m.left;
        for (int c = 0; c < cols; ++c)
        {
            rects.push_back(Rect{x, y, plot_w, plot_h});
            x += plot_w + gap(spacing.col_gaps, c);
        }
        y += plot_h + gap(spacing.row_gaps, r);
    }
    return rects;
}

}   // namespace spectra
