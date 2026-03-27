#ifdef SPECTRA_USE_IMGUI

    #include "data_interaction.hpp"

    #include <algorithm>
    #include <cmath>
    #include <imgui.h>
    #include <limits>
    #include <spectra/axes.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/series.hpp>

    #include "ui/data/axis_link.hpp"

namespace spectra
{

void DataInteraction::set_fonts(ImFont* body, ImFont* heading, ImFont* icon)
{
    tooltip_.set_fonts(body, heading);
    region_.set_fonts(body, heading);
    legend_.set_fonts(body, icon);
}

bool DataInteraction::select_point(const Series* series, size_t point_index)
{
    if (!series)
        return false;

    const float* x_data = nullptr;
    const float* y_data = nullptr;
    size_t       count  = 0;

    if (auto* ls = dynamic_cast<const LineSeries*>(series))
    {
        x_data = ls->x_data().data();
        y_data = ls->y_data().data();
        count  = ls->point_count();
    }
    else if (auto* sc = dynamic_cast<const ScatterSeries*>(series))
    {
        x_data = sc->x_data().data();
        y_data = sc->y_data().data();
        count  = sc->point_count();
    }
    else
    {
        // Point highlighting currently supports 2D series only.
        return false;
    }

    if (!x_data || !y_data || point_index >= count)
        return false;

    // Find the axes that owns this series
    const Axes* target_axes = nullptr;
    if (last_figure_)
    {
        for (auto& axes_ptr : last_figure_->axes())
        {
            if (!axes_ptr)
                continue;
            for (auto& s : axes_ptr->series())
            {
                if (s.get() == series)
                {
                    target_axes = axes_ptr.get();
                    break;
                }
            }
            if (target_axes)
                break;
        }
    }

    // Compute dy/dx at the selected point
    float dy_dx       = 0.0f;
    bool  dy_dx_valid = false;
    if (count >= 2)
    {
        size_t i = point_index;
        if (i > 0 && i + 1 < count)
        {
            float dx = x_data[i + 1] - x_data[i - 1];
            if (std::abs(dx) > 1e-30f)
            {
                dy_dx       = (y_data[i + 1] - y_data[i - 1]) / dx;
                dy_dx_valid = true;
            }
        }
        else if (i == 0)
        {
            float dx = x_data[1] - x_data[0];
            if (std::abs(dx) > 1e-30f)
            {
                dy_dx       = (y_data[1] - y_data[0]) / dx;
                dy_dx_valid = true;
            }
        }
        else if (i == count - 1)
        {
            float dx = x_data[count - 1] - x_data[count - 2];
            if (std::abs(dx) > 1e-30f)
            {
                dy_dx       = (y_data[count - 1] - y_data[count - 2]) / dx;
                dy_dx_valid = true;
            }
        }
    }

    markers_.clear();
    markers_.add(x_data[point_index],
                 y_data[point_index],
                 series,
                 point_index,
                 target_axes,
                 dy_dx,
                 dy_dx_valid);

    // Keep nearest cache coherent so tooltip/cursor feedback remains aligned.
    nearest_.found       = true;
    nearest_.series      = series;
    nearest_.point_index = point_index;
    nearest_.data_x      = x_data[point_index];
    nearest_.data_y      = y_data[point_index];

    return true;
}

void DataInteraction::set_transition_engine(TransitionEngine* te)
{
    region_.set_transition_engine(te);
    legend_.set_transition_engine(te);
}

void DataInteraction::update(const CursorReadout& cursor, Figure& figure)
{
    last_cursor_ = cursor;
    last_figure_ = &figure;

    // Update legend animation state
    float dt = 0.016f;   // fallback
    #ifdef SPECTRA_USE_IMGUI
    dt = ImGui::GetIO().DeltaTime;
    #endif
    legend_.update(dt, figure);

    // Determine which axes the cursor is over by hit-testing viewports
    active_axes_ = nullptr;
    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        const auto& vp = axes_ptr->viewport();
        float       sx = static_cast<float>(cursor.screen_x);
        float       sy = static_cast<float>(cursor.screen_y);
        if (cursor.valid && sx >= vp.x && sx <= vp.x + vp.w && sy >= vp.y && sy <= vp.y + vp.h)
        {
            active_axes_     = axes_ptr.get();
            active_viewport_ = vp;
            auto xl          = axes_ptr->x_limits();
            auto yl          = axes_ptr->y_limits();
            xlim_min_        = xl.min;
            xlim_max_        = xl.max;
            ylim_min_        = yl.min;
            ylim_max_        = yl.max;
            break;
        }
    }

    // Broadcast shared cursor to linked axes
    if (axis_link_mgr_ && active_axes_ && cursor.valid)
    {
        SharedCursor sc;
        sc.valid  = true;
        sc.data_x = xlim_min_
                    + (static_cast<float>(cursor.screen_x) - active_viewport_.x)
                          / active_viewport_.w * (xlim_max_ - xlim_min_);
        sc.data_y = ylim_max_
                    - (static_cast<float>(cursor.screen_y) - active_viewport_.y)
                          / active_viewport_.h * (ylim_max_ - ylim_min_);
        sc.screen_x    = cursor.screen_x;
        sc.screen_y    = cursor.screen_y;
        sc.source_axes = active_axes_;
        axis_link_mgr_->update_shared_cursor(sc);
    }
    else if (axis_link_mgr_)
    {
        axis_link_mgr_->clear_shared_cursor();
    }

    // Run nearest-point query
    nearest_ = find_nearest(cursor, figure);
}

void DataInteraction::draw_legend_for_figure(Figure& figure)
{
    const auto& config = figure.legend();
    if (!config.visible)
        return;
    if (config.position == LegendPosition::None)
        return;

    uintptr_t fig_id = reinterpret_cast<uintptr_t>(&figure);
    size_t    idx    = 0;
    for (auto& axes_ptr : figure.axes_mut())
    {
        if (axes_ptr)
        {
            legend_.draw(*axes_ptr, axes_ptr->viewport(), idx, config, fig_id);
        }
        ++idx;
    }
}

void DataInteraction::draw_overlays(float       window_width,
                                    float       window_height,
                                    Figure*     current_figure,
                                    ImDrawList* dl)
{
    Figure* overlay_figure = current_figure ? current_figure : last_figure_;
    if (current_figure)
        last_figure_ = current_figure;

    // Draw legend interaction for each axes (gated on figure legend visibility)
    if (overlay_figure)
    {
        draw_legend_for_figure(*overlay_figure);
    }

    // Draw markers (data tips) — always visible, even when cursor is outside the figure.
    // Each marker is drawn using its owning axes' viewport and limits so that
    // markers stay in their correct subplot regardless of cursor position.
    if (overlay_figure && !markers_.markers().empty())
    {
        for (auto& axes_ptr : overlay_figure->axes())
        {
            if (!axes_ptr)
                continue;
            const auto& vp = axes_ptr->viewport();
            auto        xl = axes_ptr->x_limits();
            auto        yl = axes_ptr->y_limits();
            markers_.draw(vp,
                          static_cast<float>(xl.min),
                          static_cast<float>(xl.max),
                          static_cast<float>(yl.min),
                          static_cast<float>(yl.max),
                          1.0f,
                          axes_ptr.get(),
                          dl);
        }
    }

    // Draw region selection overlay — use the axes where the ROI was started
    // so it stays in the correct subplot when the cursor moves.
    Axes* roi_axes = region_axes_ ? region_axes_ : active_axes_;
    if (roi_axes && (region_.is_dragging() || region_.has_selection()))
    {
        const auto& vp = roi_axes->viewport();
        auto        xl = roi_axes->x_limits();
        auto        yl = roi_axes->y_limits();
        region_.draw(vp,
                     static_cast<float>(xl.min),
                     static_cast<float>(xl.max),
                     static_cast<float>(yl.min),
                     static_cast<float>(yl.max),
                     window_width,
                     window_height);
    }

    // Draw crosshair: use multi-axes mode if figure has multiple axes
    if (overlay_figure && overlay_figure->axes().size() > 1)
    {
        crosshair_.draw_all_axes(last_cursor_, *overlay_figure, axis_link_mgr_, dl);
    }
    else if (active_axes_)
    {
        crosshair_.draw(
            last_cursor_, active_viewport_, xlim_min_, xlim_max_, ylim_min_, ylim_max_, dl);
    }

    // Draw tooltip last (on top)
    tooltip_.draw(nearest_, window_width, window_height, dl);
}

bool DataInteraction::dispatch_series_selection_from_nearest()
{
    if (!nearest_.found || !nearest_.series || !last_figure_)
        return false;

    int ax_idx = 0;
    for (auto& axes_ptr : last_figure_->axes())
    {
        if (!axes_ptr)
        {
            ax_idx++;
            continue;
        }
        int s_idx = 0;
        for (auto& series_ptr : axes_ptr->series())
        {
            if (series_ptr.get() == nearest_.series)
            {
                if (on_series_selected_)
                {
                    on_series_selected_(last_figure_,
                                        axes_ptr.get(),
                                        ax_idx,
                                        series_ptr.get(),
                                        s_idx);
                }
                if (on_point_selected_)
                {
                    on_point_selected_(last_figure_,
                                       axes_ptr.get(),
                                       ax_idx,
                                       series_ptr.get(),
                                       s_idx,
                                       nearest_.point_index);
                }
                return true;
            }
            s_idx++;
        }
        ax_idx++;
    }

    // Fallback: nearest point selected, but series lookup path above didn't match.
    if (on_point_selected_ && active_axes_)
    {
        on_point_selected_(last_figure_,
                           active_axes_,
                           0,
                           const_cast<Series*>(nearest_.series),
                           0,
                           nearest_.point_index);
        return true;
    }

    return false;
}

bool DataInteraction::on_mouse_click_datatip_only(int button, double screen_x, double screen_y)
{
    if (!active_axes_ || !last_figure_)
        return false;

    if (button == 0)
    {
        int marker_hit = markers_.hit_test(static_cast<float>(screen_x),
                                           static_cast<float>(screen_y),
                                           active_viewport_,
                                           xlim_min_,
                                           xlim_max_,
                                           ylim_min_,
                                           ylim_max_,
                                           10.0f,
                                           active_axes_);
        if (marker_hit >= 0)
        {
            markers_.remove(static_cast<size_t>(marker_hit));
            return true;
        }

        constexpr float SELECT_SNAP_PX = 30.0f;
        if (nearest_.found && nearest_.distance_px <= SELECT_SNAP_PX)
        {
            markers_.toggle_or_add(nearest_.data_x,
                                   nearest_.data_y,
                                   nearest_.series,
                                   nearest_.point_index,
                                   active_axes_,
                                   nearest_.dy_dx,
                                   nearest_.dy_dx_valid);
            return true;
        }
    }

    // Right click: remove marker
    if (button == 1)
    {
        int idx = markers_.hit_test(static_cast<float>(screen_x),
                                    static_cast<float>(screen_y),
                                    active_viewport_,
                                    xlim_min_,
                                    xlim_max_,
                                    ylim_min_,
                                    ylim_max_,
                                    10.0f,
                                    active_axes_);
        if (idx >= 0)
        {
            markers_.remove(static_cast<size_t>(idx));
            return true;
        }
    }

    return false;
}

bool DataInteraction::on_mouse_click_series_only(double screen_x, double screen_y)
{
    (void)screen_x;
    (void)screen_y;

    if (!active_axes_ || !last_figure_)
        return false;

    constexpr float SELECT_SNAP_PX = 30.0f;
    if (nearest_.found && nearest_.distance_px <= SELECT_SNAP_PX)
    {
        return dispatch_series_selection_from_nearest();
    }

    if (on_series_deselected_)
    {
        on_series_deselected_();
        return true;
    }

    return false;
}

bool DataInteraction::on_mouse_click(int button, double screen_x, double screen_y)
{
    if (!active_axes_ || !last_figure_)
        return false;

    // Left click: remove an existing data tip if clicked on it, otherwise pin a new one
    if (button == 0)
    {
        // First: hit-test existing markers — clicking on a data tip removes it
        int marker_hit = markers_.hit_test(static_cast<float>(screen_x),
                                           static_cast<float>(screen_y),
                                           active_viewport_,
                                           xlim_min_,
                                           xlim_max_,
                                           ylim_min_,
                                           ylim_max_,
                                           10.0f,
                                           active_axes_);
        if (marker_hit >= 0)
        {
            markers_.remove(static_cast<size_t>(marker_hit));
            return true;
        }

        constexpr float SELECT_SNAP_PX = 30.0f;
        if (nearest_.found && nearest_.distance_px <= SELECT_SNAP_PX)
        {
            markers_.toggle_or_add(nearest_.data_x,
                                   nearest_.data_y,
                                   nearest_.series,
                                   nearest_.point_index,
                                   active_axes_,
                                   nearest_.dy_dx,
                                   nearest_.dy_dx_valid);

            // Also fire series/point selection callbacks (for inspector + data editor sync).
            dispatch_series_selection_from_nearest();
            return true;
        }
        else if (on_series_deselected_)
        {
            // Clicked on canvas but not near any series — deselect
            on_series_deselected_();
            return true;
        }
    }

    // Right click: remove marker
    if (button == 1)
    {
        int idx = markers_.hit_test(static_cast<float>(screen_x),
                                    static_cast<float>(screen_y),
                                    active_viewport_,
                                    xlim_min_,
                                    xlim_max_,
                                    ylim_min_,
                                    ylim_max_,
                                    10.0f,
                                    active_axes_);
        if (idx >= 0)
        {
            markers_.remove(static_cast<size_t>(idx));
            return true;
        }
    }

    return false;
}

void DataInteraction::add_marker(float data_x, float data_y, const Series* series, size_t index)
{
    markers_.add(data_x, data_y, series, index);
}

void DataInteraction::remove_marker(size_t idx)
{
    markers_.remove(idx);
}

void DataInteraction::clear_markers()
{
    markers_.clear();
}

void DataInteraction::set_snap_radius(float px)
{
    tooltip_.set_snap_radius(px);
}

// ─── Region selection ───────────────────────────────────────────────────────

void DataInteraction::begin_region_select(double screen_x, double screen_y)
{
    if (!active_axes_)
        return;
    region_axes_ = active_axes_;
    region_.begin(screen_x, screen_y, active_viewport_, xlim_min_, xlim_max_, ylim_min_, ylim_max_);
}

void DataInteraction::update_region_drag(double screen_x, double screen_y)
{
    // Use the axes where the ROI was started, not whatever the cursor is over now
    if (!region_axes_)
        return;
    const auto& vp = region_axes_->viewport();
    auto        xl = region_axes_->x_limits();
    auto        yl = region_axes_->y_limits();
    region_.update_drag(screen_x,
                        screen_y,
                        vp,
                        static_cast<float>(xl.min),
                        static_cast<float>(xl.max),
                        static_cast<float>(yl.min),
                        static_cast<float>(yl.max));
}

void DataInteraction::finish_region_select()
{
    region_.finish(region_axes_ ? region_axes_ : active_axes_);
}

void DataInteraction::dismiss_region_select()
{
    region_.dismiss();
    region_axes_ = nullptr;
}

void DataInteraction::select_series_in_rect(const BoxZoomRect& rect, Figure& figure)
{
    std::vector<RectSelectedEntry> hits;

    // Normalize the screen-space rectangle
    double rx0 = std::min(rect.x0, rect.x1);
    double ry0 = std::min(rect.y0, rect.y1);
    double rx1 = std::max(rect.x0, rect.x1);
    double ry1 = std::max(rect.y0, rect.y1);

    int ax_idx = 0;
    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
        {
            ax_idx++;
            continue;
        }
        const auto& vp   = axes_ptr->viewport();
        auto        xlim = axes_ptr->x_limits();
        auto        ylim = axes_ptr->y_limits();
        double      xr   = xlim.max - xlim.min;
        double      yr   = ylim.max - ylim.min;
        if (xr == 0.0)
            xr = 1.0;
        if (yr == 0.0)
            yr = 1.0;

        int s_idx = 0;
        for (auto& series_ptr : axes_ptr->series())
        {
            if (!series_ptr || !series_ptr->visible())
            {
                s_idx++;
                continue;
            }

            const float* x_data = nullptr;
            const float* y_data = nullptr;
            size_t       count  = 0;

            if (auto* ls = dynamic_cast<LineSeries*>(series_ptr.get()))
            {
                x_data = ls->x_data().data();
                y_data = ls->y_data().data();
                count  = ls->point_count();
            }
            else if (auto* sc = dynamic_cast<ScatterSeries*>(series_ptr.get()))
            {
                x_data = sc->x_data().data();
                y_data = sc->y_data().data();
                count  = sc->point_count();
            }

            if (!x_data || !y_data || count == 0)
            {
                s_idx++;
                continue;
            }

            // Check if any data point maps to a screen position inside the rectangle
            bool hit = false;
            for (size_t i = 0; i < count && !hit; ++i)
            {
                double norm_x = (static_cast<double>(x_data[i]) - xlim.min) / xr;
                double norm_y = (static_cast<double>(y_data[i]) - ylim.min) / yr;
                double scr_x  = vp.x + norm_x * vp.w;
                double scr_y  = vp.y + (1.0 - norm_y) * vp.h;
                if (scr_x >= rx0 && scr_x <= rx1 && scr_y >= ry0 && scr_y <= ry1)
                    hit = true;
            }

            // Also check line segment intersections with the rectangle edges
            if (!hit && count >= 2)
            {
                for (size_t i = 0; i + 1 < count && !hit; ++i)
                {
                    double nx0 = (static_cast<double>(x_data[i]) - xlim.min) / xr;
                    double ny0 = (static_cast<double>(y_data[i]) - ylim.min) / yr;
                    double nx1 = (static_cast<double>(x_data[i + 1]) - xlim.min) / xr;
                    double ny1 = (static_cast<double>(y_data[i + 1]) - ylim.min) / yr;

                    double sx0 = vp.x + nx0 * vp.w;
                    double sy0 = vp.y + (1.0 - ny0) * vp.h;
                    double sx1 = vp.x + nx1 * vp.w;
                    double sy1 = vp.y + (1.0 - ny1) * vp.h;

                    // Cohen-Sutherland: if the segment can be clipped to the rectangle, it
                    // intersects
                    double cx0 = sx0, cy0 = sy0, cx1 = sx1, cy1 = sy1;
                    auto   outcode = [&](double px, double py) -> int
                    {
                        int code = 0;
                        if (px < rx0)
                            code |= 1;
                        else if (px > rx1)
                            code |= 2;
                        if (py < ry0)
                            code |= 4;
                        else if (py > ry1)
                            code |= 8;
                        return code;
                    };
                    int oc0 = outcode(cx0, cy0);
                    int oc1 = outcode(cx1, cy1);
                    for (int iter = 0; iter < 20; ++iter)
                    {
                        if ((oc0 | oc1) == 0)
                        {
                            hit = true;
                            break;
                        }
                        if ((oc0 & oc1) != 0)
                            break;
                        int    oc_out = oc0 ? oc0 : oc1;
                        double px = 0, py = 0;
                        if (oc_out & 8)
                        {
                            px = cx0 + (cx1 - cx0) * (ry1 - cy0) / (cy1 - cy0);
                            py = ry1;
                        }
                        else if (oc_out & 4)
                        {
                            px = cx0 + (cx1 - cx0) * (ry0 - cy0) / (cy1 - cy0);
                            py = ry0;
                        }
                        else if (oc_out & 2)
                        {
                            py = cy0 + (cy1 - cy0) * (rx1 - cx0) / (cx1 - cx0);
                            px = rx1;
                        }
                        else if (oc_out & 1)
                        {
                            py = cy0 + (cy1 - cy0) * (rx0 - cx0) / (cx1 - cx0);
                            px = rx0;
                        }
                        if (oc_out == oc0)
                        {
                            cx0 = px;
                            cy0 = py;
                            oc0 = outcode(cx0, cy0);
                        }
                        else
                        {
                            cx1 = px;
                            cy1 = py;
                            oc1 = outcode(cx1, cy1);
                        }
                    }
                }
            }

            if (hit)
            {
                hits.push_back({&figure, axes_ptr.get(), ax_idx, series_ptr.get(), s_idx});
            }
            s_idx++;
        }
        ax_idx++;
    }

    if (on_rect_series_selected_)
        on_rect_series_selected_(hits);
}

NearestPointResult DataInteraction::find_nearest(const CursorReadout& cursor, Figure& figure) const
{
    NearestPointResult best;
    best.found       = false;
    best.distance_px = std::numeric_limits<float>::max();

    if (!cursor.valid)
        return best;

    float cx = static_cast<float>(cursor.screen_x);
    float cy = static_cast<float>(cursor.screen_y);

    // Search all axes
    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        const auto& vp = axes_ptr->viewport();

        // Only search axes the cursor is inside
        if (cx < vp.x || cx > vp.x + vp.w || cy < vp.y || cy > vp.y + vp.h)
            continue;

        auto   xlim    = axes_ptr->x_limits();
        auto   ylim    = axes_ptr->y_limits();
        double x_range = xlim.max - xlim.min;
        double y_range = ylim.max - ylim.min;
        if (x_range == 0.0)
            x_range = 1.0;
        if (y_range == 0.0)
            y_range = 1.0;

        for (auto& series_ptr : axes_ptr->series())
        {
            if (!series_ptr || !series_ptr->visible())
                continue;

            const float* x_data = nullptr;
            const float* y_data = nullptr;
            size_t       count  = 0;

            if (auto* ls = dynamic_cast<LineSeries*>(series_ptr.get()))
            {
                x_data = ls->x_data().data();
                y_data = ls->y_data().data();
                count  = ls->point_count();
            }
            else if (auto* sc = dynamic_cast<ScatterSeries*>(series_ptr.get()))
            {
                x_data = sc->x_data().data();
                y_data = sc->y_data().data();
                count  = sc->point_count();
            }

            if (!x_data || !y_data || count == 0)
                continue;

            // Linear scan for nearest point (screen-space distance)
            for (size_t i = 0; i < count; ++i)
            {
                // Convert data point to screen coordinates
                float norm_x = (x_data[i] - xlim.min) / x_range;
                float norm_y = (y_data[i] - ylim.min) / y_range;
                float sx     = vp.x + norm_x * vp.w;
                float sy     = vp.y + (1.0f - norm_y) * vp.h;

                float dx   = cx - sx;
                float dy   = cy - sy;
                float dist = std::sqrt(dx * dx + dy * dy);

                if (dist < best.distance_px)
                {
                    best.found       = true;
                    best.series      = series_ptr.get();
                    best.point_index = i;
                    best.data_x      = x_data[i];
                    best.data_y      = y_data[i];
                    best.screen_x    = sx;
                    best.screen_y    = sy;
                    best.distance_px = dist;
                }
            }
        }
    }

    // Compute dy/dx at the nearest point via finite difference
    if (best.found && best.series)
    {
        const float* xd    = nullptr;
        const float* yd    = nullptr;
        size_t       count = 0;
        if (auto* ls = dynamic_cast<const LineSeries*>(best.series))
        {
            xd    = ls->x_data().data();
            yd    = ls->y_data().data();
            count = ls->point_count();
        }
        else if (auto* sc = dynamic_cast<const ScatterSeries*>(best.series))
        {
            xd    = sc->x_data().data();
            yd    = sc->y_data().data();
            count = sc->point_count();
        }

        if (xd && yd && count >= 2)
        {
            size_t i = best.point_index;
            if (i > 0 && i + 1 < count)
            {
                // Central difference
                float dx = xd[i + 1] - xd[i - 1];
                if (std::abs(dx) > 1e-30f)
                {
                    best.dy_dx       = (yd[i + 1] - yd[i - 1]) / dx;
                    best.dy_dx_valid = true;
                }
            }
            else if (i == 0 && count >= 2)
            {
                // Forward difference
                float dx = xd[1] - xd[0];
                if (std::abs(dx) > 1e-30f)
                {
                    best.dy_dx       = (yd[1] - yd[0]) / dx;
                    best.dy_dx_valid = true;
                }
            }
            else if (i == count - 1 && count >= 2)
            {
                // Backward difference
                float dx = xd[count - 1] - xd[count - 2];
                if (std::abs(dx) > 1e-30f)
                {
                    best.dy_dx       = (yd[count - 1] - yd[count - 2]) / dx;
                    best.dy_dx_valid = true;
                }
            }
        }
    }

    return best;
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
