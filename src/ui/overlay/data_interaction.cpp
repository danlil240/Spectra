#ifdef SPECTRA_USE_IMGUI

    #include "data_interaction.hpp"

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

            // Cache for persistent marker drawing when cursor leaves
            has_marker_viewport_ = true;
            marker_viewport_     = vp;
            marker_xlim_min_     = xl.min;
            marker_xlim_max_     = xl.max;
            marker_ylim_min_     = yl.min;
            marker_ylim_max_     = yl.max;
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

void DataInteraction::draw_overlays(float window_width, float window_height)
{
    // Draw legend interaction for each axes (gated on figure legend visibility)
    if (last_figure_)
    {
        draw_legend_for_figure(*last_figure_);
    }

    // Draw markers (data tips) — always visible, even when cursor is outside the figure.
    // Use live viewport when cursor is over axes, otherwise use cached viewport.
    if (active_axes_)
    {
        markers_.draw(active_viewport_, xlim_min_, xlim_max_, ylim_min_, ylim_max_);
    }
    else if (has_marker_viewport_ && !markers_.markers().empty())
    {
        // Cursor left the figure — keep drawing markers at their last known positions.
        // Update cached limits from the figure's first axes (zoom/pan may have changed).
        if (last_figure_ && !last_figure_->axes().empty() && last_figure_->axes()[0])
        {
            auto& ax         = last_figure_->axes()[0];
            marker_viewport_ = ax->viewport();
            auto xl          = ax->x_limits();
            auto yl          = ax->y_limits();
            marker_xlim_min_ = xl.min;
            marker_xlim_max_ = xl.max;
            marker_ylim_min_ = yl.min;
            marker_ylim_max_ = yl.max;
        }
        markers_.draw(marker_viewport_,
                      marker_xlim_min_,
                      marker_xlim_max_,
                      marker_ylim_min_,
                      marker_ylim_max_);
    }

    // Draw region selection overlay
    if (active_axes_)
    {
        region_.draw(active_viewport_,
                     xlim_min_,
                     xlim_max_,
                     ylim_min_,
                     ylim_max_,
                     window_width,
                     window_height);
    }

    // Draw crosshair: use multi-axes mode if figure has multiple axes
    if (last_figure_ && last_figure_->axes().size() > 1)
    {
        crosshair_.draw_all_axes(last_cursor_, *last_figure_, axis_link_mgr_);
    }
    else if (active_axes_)
    {
        crosshair_.draw(last_cursor_, active_viewport_, xlim_min_, xlim_max_, ylim_min_, ylim_max_);
    }

    // Draw tooltip last (on top)
    tooltip_.draw(nearest_, window_width, window_height);
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
                                           ylim_max_);
        if (marker_hit >= 0)
        {
            markers_.remove(static_cast<size_t>(marker_hit));
            return true;
        }

        constexpr float SELECT_SNAP_PX = 30.0f;
        if (nearest_.found && nearest_.distance_px <= SELECT_SNAP_PX)
        {
            // Toggle a persistent data label (datatip) on the clicked point
            markers_.toggle_or_add(nearest_.data_x,
                                   nearest_.data_y,
                                   nearest_.series,
                                   nearest_.point_index);

            // Also fire series selection callback (for inspector)
            if (on_series_selected_)
            {
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
                            on_series_selected_(last_figure_,
                                                axes_ptr.get(),
                                                ax_idx,
                                                series_ptr.get(),
                                                s_idx);
                            return true;
                        }
                        s_idx++;
                    }
                    ax_idx++;
                }
            }
            return true;
        }
        else if (on_series_deselected_)
        {
            // Clicked on canvas but not near any series — deselect
            on_series_deselected_();
            return true;
        }
    }

    // Right click: select nearest series (for context menu) or remove marker
    if (button == 1)
    {
        bool rc_selected_series = false;

        // First try to select the nearest series so context menu has a target
        // Use the right-click callback (no-toggle) so it always selects, never deselects
        constexpr float RC_SELECT_SNAP_PX = 40.0f;
        auto& rc_cb = on_series_rc_selected_ ? on_series_rc_selected_ : on_series_selected_;
        if (nearest_.found && nearest_.distance_px <= RC_SELECT_SNAP_PX && rc_cb)
        {
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
                        rc_cb(last_figure_, axes_ptr.get(), ax_idx, series_ptr.get(), s_idx);
                        rc_selected_series = true;
                        goto right_click_marker_check;
                    }
                    s_idx++;
                }
                ax_idx++;
            }
        }

    right_click_marker_check:
        int idx = markers_.hit_test(static_cast<float>(screen_x),
                                    static_cast<float>(screen_y),
                                    active_viewport_,
                                    xlim_min_,
                                    xlim_max_,
                                    ylim_min_,
                                    ylim_max_);
        if (idx >= 0)
        {
            markers_.remove(static_cast<size_t>(idx));
            return true;
        }

        // If we selected a series, consume the event so the input handler
        // doesn't start zoom drag — the context menu will open via ImGui instead
        if (rc_selected_series)
            return true;
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
    region_.begin(screen_x, screen_y, active_viewport_, xlim_min_, xlim_max_, ylim_min_, ylim_max_);
}

void DataInteraction::update_region_drag(double screen_x, double screen_y)
{
    if (!active_axes_)
        return;
    region_.update_drag(screen_x,
                        screen_y,
                        active_viewport_,
                        xlim_min_,
                        xlim_max_,
                        ylim_min_,
                        ylim_max_);
}

void DataInteraction::finish_region_select()
{
    region_.finish(active_axes_);
}

void DataInteraction::dismiss_region_select()
{
    region_.dismiss();
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

        auto  xlim    = axes_ptr->x_limits();
        auto  ylim    = axes_ptr->y_limits();
        float x_range = xlim.max - xlim.min;
        float y_range = ylim.max - ylim.min;
        if (x_range == 0.0f)
            x_range = 1.0f;
        if (y_range == 0.0f)
            y_range = 1.0f;

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

    return best;
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
