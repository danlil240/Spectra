#ifdef SPECTRA_USE_IMGUI

    #include "data_interaction.hpp"

    #include <algorithm>
    #include <cmath>
    #include <imgui.h>
    #include <limits>
    #include <spectra/axes.hpp>
    #include <spectra/axes3d.hpp>
    #include <spectra/figure.hpp>
    #include <optional>
    #include <spectra/chunked_series.hpp>
    #include <spectra/series.hpp>
    #include <spectra/series3d.hpp>
    #include <spectra/series_image.hpp>
    #include <spectra/series_stats.hpp>

    #include "core/axis_scale.hpp"
    #include "axes3d_pick.hpp"
    #include "crosshair_mapping.hpp"
    #include "series_probe.hpp"
    #include "ui/data/axis_link.hpp"

namespace spectra
{

void DataInteraction::set_fonts(ImFont* body, ImFont* heading, ImFont* icon)
{
    tooltip_.set_fonts(body, heading);
    compare_.set_fonts(body);
    region_.set_fonts(body, heading);
    legend_.set_fonts(body, icon);
    annotations_.set_fonts(body, heading);
}

void DataInteraction::set_theme_manager(ui::ThemeManager* tm)
{
    theme_mgr_ = tm;
    crosshair_.set_theme_manager(tm);
    compare_.set_theme_manager(tm);
    tooltip_.set_theme_manager(tm);
    markers_.set_theme_manager(tm);
    annotations_.set_theme_manager(tm);
    region_.set_theme_manager(tm);
    legend_.set_theme_manager(tm);
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

    // Absolute x/y = series x_offset/y_offset + stored relative float.
    const double abs_x = static_cast<double>(x_data[point_index]) + series->x_offset();
    const double abs_y = static_cast<double>(y_data[point_index]) + series->y_offset();

    markers_.clear();
    markers_.add(abs_x, abs_y, series, point_index, target_axes, dy_dx, dy_dx_valid);

    // Keep nearest cache coherent so tooltip/cursor feedback remains aligned.
    nearest_.found       = true;
    nearest_.series      = series;
    nearest_.point_index = point_index;
    nearest_.data_x      = abs_x;
    nearest_.data_y      = abs_y;

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

    // Determine which axes the cursor is over by hit-testing viewports (2D then 3D)
    active_axes_      = nullptr;
    active_axes3d_    = nullptr;
    auto sx           = static_cast<float>(cursor.screen_x);
    auto sy           = static_cast<float>(cursor.screen_y);
    auto try_axes_hit = [&](AxesBase* axes_base) -> bool
    {
        if (!axes_base || !cursor.valid)
            return false;
        const auto& vp = axes_base->viewport();
        if (sx < vp.x || sx > vp.x + vp.w || sy < vp.y || sy > vp.y + vp.h)
            return false;
        active_viewport_ = vp;
        if (auto* axes2d = dynamic_cast<Axes*>(axes_base))
        {
            active_axes_ = axes2d;
            auto xl      = axes2d->x_limits();
            auto yl      = axes2d->y_limits();
            xlim_min_    = xl.min;
            xlim_max_    = xl.max;
            ylim_min_    = yl.min;
            ylim_max_    = yl.max;
        }
        else if (auto* axes3d = dynamic_cast<Axes3D*>(axes_base))
        {
            active_axes3d_ = axes3d;
        }
        return true;
    };

    for (auto& axes_ptr : figure.axes())
    {
        if (try_axes_hit(axes_ptr.get()))
            break;
    }
    if (!active_axes_ && !active_axes3d_)
    {
        for (auto& axes_ptr : figure.all_axes())
        {
            if (try_axes_hit(axes_ptr.get()))
                break;
        }
    }

    // Broadcast shared cursor to linked axes
    if (axis_link_mgr_ && active_axes_ && cursor.valid)
    {
        SharedCursor sc;
        sc.valid       = true;
        sc.data_x      = crosshair_mapping::screen_to_data_x(*active_axes_, cursor.screen_x);
        sc.data_y      = crosshair_mapping::screen_to_data_y(*active_axes_, cursor.screen_y);
        sc.screen_x    = cursor.screen_x;
        sc.screen_y    = cursor.screen_y;
        sc.source_axes = active_axes_;
        axis_link_mgr_->update_shared_cursor(sc);
    }
    else if (axis_link_mgr_)
    {
        axis_link_mgr_->clear_shared_cursor();
    }

    // Run nearest-point query (skip when pointer is outside the plot/window)
    if (!cursor.valid)
        nearest_ = {};
    else
        nearest_ = find_nearest(cursor, figure);
}

bool DataInteraction::refresh_pointer_state(double screen_x, double screen_y)
{
    if (!last_figure_)
        return false;

    CursorReadout cursor;
    cursor.screen_x = screen_x;
    cursor.screen_y = screen_y;

    active_axes_   = nullptr;
    active_axes3d_ = nullptr;
    const auto sx  = static_cast<float>(screen_x);
    const auto sy  = static_cast<float>(screen_y);

    auto try_refresh_hit = [&](AxesBase* axes_base) -> bool
    {
        if (!axes_base)
            return false;
        const auto& vp = axes_base->viewport();
        if (vp.w <= 0.0f || vp.h <= 0.0f)
            return false;
        if (sx < vp.x || sx > vp.x + vp.w || sy < vp.y || sy > vp.y + vp.h)
            return false;

        active_viewport_ = vp;
        cursor.valid     = true;
        cursor.screen_x  = screen_x;
        cursor.screen_y  = screen_y;

        if (auto* axes2d = dynamic_cast<Axes*>(axes_base))
        {
            active_axes_ = axes2d;
            auto xl      = axes2d->x_limits();
            auto yl      = axes2d->y_limits();
            xlim_min_    = xl.min;
            xlim_max_    = xl.max;
            ylim_min_    = yl.min;
            ylim_max_    = yl.max;

            cursor.data_x = crosshair_mapping::screen_to_data_x(*axes2d, screen_x);
            cursor.data_y = crosshair_mapping::screen_to_data_y(*axes2d, screen_y);
        }
        else if (auto* axes3d = dynamic_cast<Axes3D*>(axes_base))
        {
            active_axes3d_ = axes3d;
        }
        return true;
    };

    for (auto& axes_ptr : last_figure_->axes())
    {
        if (try_refresh_hit(axes_ptr.get()))
            break;
    }
    if (!active_axes_ && !active_axes3d_)
    {
        for (auto& axes_ptr : last_figure_->all_axes())
        {
            if (try_refresh_hit(axes_ptr.get()))
                break;
        }
    }

    last_cursor_ = cursor;
    nearest_     = find_nearest(cursor, *last_figure_);
    return cursor.valid;
}

void DataInteraction::draw_legend_for_figure(Figure& figure)
{
    const auto& config = figure.legend();
    if (!config.visible)
        return;
    if (config.position == LegendPosition::None)
        return;

    auto   fig_id = reinterpret_cast<uintptr_t>(&figure);
    size_t idx    = 0;
    for (auto& axes_ptr : figure.axes_mut())
    {
        if (axes_ptr)
        {
            legend_.draw(*axes_ptr, axes_ptr->viewport(), idx, config, fig_id, &figure.style());
        }
        ++idx;
    }
    // 3D axes (all_axes()) get a legend too; their indices continue after the
    // 2D ones so drag offsets and ImGui ids stay distinct.
    for (auto& axes_ptr : figure.all_axes())
    {
        if (axes_ptr)
        {
            legend_.draw(*axes_ptr, axes_ptr->viewport(), idx, config, fig_id, &figure.style());
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
            markers_.draw(AxisMapping(*axes_ptr), 1.0f, axes_ptr.get(), dl);
        }
        for (auto& axes_ptr : overlay_figure->all_axes())
        {
            if (!axes_ptr)
                continue;
            if (auto* axes3d = dynamic_cast<Axes3D*>(axes_ptr.get()))
                markers_.draw_3d(*axes3d, 1.0f, dl);
        }
    }

    // Draw region selection overlay — use the axes where the ROI was started
    // so it stays in the correct subplot when the cursor moves.
    Axes* roi_axes = region_axes_ ? region_axes_ : active_axes_;
    if (roi_axes && (region_.is_dragging() || region_.has_selection()))
    {
        region_.draw(AxisMapping(*roi_axes), window_width, window_height);
    }

    // Draw crosshair: use multi-axes mode if figure has multiple axes
    if (overlay_figure && overlay_figure->axes().size() > 1)
    {
        crosshair_.draw_all_axes(last_cursor_, *overlay_figure, axis_link_mgr_, dl);
    }
    else if (active_axes_)
    {
        crosshair_.draw(last_cursor_,
                        active_viewport_,
                        xlim_min_,
                        xlim_max_,
                        ylim_min_,
                        ylim_max_,
                        dl,
                        active_axes_);
    }

    // Draw annotations
    if (overlay_figure)
    {
        for (auto& axes_ptr : overlay_figure->axes())
        {
            if (!axes_ptr)
                continue;
            annotations_.draw(AxisMapping(*axes_ptr), 1.0f, axes_ptr.get(), dl);
        }
    }

    // Compare-at-X readout with the crosshair: one tooltip listing every
    // series at the cursor X.  Hidden (like the tooltip) under an open popup.
    compare_drawn_ = false;
    if (crosshair_.enabled() && compare_.enabled() && !compare_suppressed_ && active_axes_
        && last_cursor_.valid && overlay_figure
        && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
    {
        compare_drawn_ = compare_.draw(last_cursor_,
                                       *overlay_figure,
                                       *active_axes_,
                                       axis_link_mgr_,
                                       crosshair_.opacity(),
                                       window_width,
                                       window_height,
                                       dl);
    }

    // Draw tooltip last (on top); the compare readout already lists the series.
    if (!compare_drawn_)
        tooltip_.draw(nearest_, window_width, window_height, dl);
}

bool DataInteraction::dispatch_series_selection_from_nearest()
{
    if (!nearest_.found || !nearest_.series || !last_figure_)
        return false;

    auto dispatch_for_axes = [&](AxesBase* axes_base, int ax_idx) -> bool
    {
        if (!axes_base)
            return false;
        int s_idx = 0;
        for (auto& series_ptr : axes_base->series())
        {
            if (series_ptr.get() == nearest_.series)
            {
                Axes* axes2d = dynamic_cast<Axes*>(axes_base);
                if (on_series_selected_)
                {
                    on_series_selected_(last_figure_, axes2d, ax_idx, series_ptr.get(), s_idx);
                }
                if (on_point_selected_)
                {
                    on_point_selected_(last_figure_,
                                       axes2d,
                                       ax_idx,
                                       series_ptr.get(),
                                       s_idx,
                                       nearest_.point_index);
                }
                return true;
            }
            s_idx++;
        }
        return false;
    };

    int ax_idx = 0;
    for (auto& axes_ptr : last_figure_->axes())
    {
        if (dispatch_for_axes(axes_ptr.get(), ax_idx))
            return true;
        ax_idx++;
    }

    ax_idx = 0;
    for (auto& axes_ptr : last_figure_->all_axes())
    {
        if (dispatch_for_axes(axes_ptr.get(), ax_idx))
            return true;
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

bool DataInteraction::try_toggle_datatip_at_nearest()
{
    constexpr float SELECT_SNAP_PX = 30.0f;
    if (!nearest_.found || nearest_.distance_px > SELECT_SNAP_PX)
        return false;

    if (nearest_.is_3d && nearest_.axes3d)
    {
        markers_.toggle_or_add_3d(nearest_.data_x,
                                  nearest_.data_y,
                                  nearest_.data_z,
                                  nearest_.series,
                                  nearest_.point_index,
                                  nearest_.axes3d);
        return true;
    }

    if (active_axes_ && nearest_.kind != HoverHitKind::Vertex && nearest_.kind != HoverHitKind::Bar
        && nearest_.kind != HoverHitKind::Bin)
    {
        // Between samples / inside an image / on a contour: pin a data tip at
        // the picked position (no stored sample to toggle).
        markers_.add(nearest_.data_x,
                     nearest_.data_y,
                     nearest_.series,
                     nearest_.point_index,
                     active_axes_,
                     nearest_.dy_dx,
                     nearest_.dy_dx_valid);
        return true;
    }

    if (active_axes_)
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

    return false;
}

bool DataInteraction::on_mouse_click_datatip_only(int button, double screen_x, double screen_y)
{
    refresh_pointer_state(screen_x, screen_y);

    if ((!active_axes_ && !active_axes3d_) || !last_figure_)
        return false;

    if (button == 0)
    {
        if (active_axes3d_)
        {
            int marker_hit = markers_.hit_test_3d(static_cast<float>(screen_x),
                                                  static_cast<float>(screen_y),
                                                  *active_axes3d_,
                                                  10.0f);
            if (marker_hit >= 0)
            {
                markers_.remove(static_cast<size_t>(marker_hit));
                return true;
            }
        }
        else if (active_axes_)
        {
            int marker_hit = markers_.hit_test(static_cast<float>(screen_x),
                                               static_cast<float>(screen_y),
                                               active_mapping(),
                                               10.0f,
                                               active_axes_);
            if (marker_hit >= 0)
            {
                markers_.remove(static_cast<size_t>(marker_hit));
                return true;
            }
        }

        if (try_toggle_datatip_at_nearest())
            return true;
    }

    // Right click: remove marker
    if (button == 1)
    {
        if (active_axes3d_)
        {
            int idx = markers_.hit_test_3d(static_cast<float>(screen_x),
                                           static_cast<float>(screen_y),
                                           *active_axes3d_,
                                           10.0f);
            if (idx >= 0)
            {
                markers_.remove(static_cast<size_t>(idx));
                return true;
            }
        }
        else if (active_axes_)
        {
            int idx = markers_.hit_test(static_cast<float>(screen_x),
                                        static_cast<float>(screen_y),
                                        active_mapping(),
                                        10.0f,
                                        active_axes_);
            if (idx >= 0)
            {
                markers_.remove(static_cast<size_t>(idx));
                return true;
            }
        }
    }

    return false;
}

bool DataInteraction::on_mouse_click_series_only(double screen_x, double screen_y)
{
    refresh_pointer_state(screen_x, screen_y);

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
    refresh_pointer_state(screen_x, screen_y);

    if ((!active_axes_ && !active_axes3d_) || !last_figure_)
        return false;

    // Left click: remove an existing data tip if clicked on it, otherwise pin a new one
    if (button == 0)
    {
        if (active_axes3d_)
        {
            int marker_hit = markers_.hit_test_3d(static_cast<float>(screen_x),
                                                  static_cast<float>(screen_y),
                                                  *active_axes3d_,
                                                  10.0f);
            if (marker_hit >= 0)
            {
                markers_.remove(static_cast<size_t>(marker_hit));
                return true;
            }
        }
        else if (active_axes_)
        {
            int marker_hit = markers_.hit_test(static_cast<float>(screen_x),
                                               static_cast<float>(screen_y),
                                               active_mapping(),
                                               10.0f,
                                               active_axes_);
            if (marker_hit >= 0)
            {
                markers_.remove(static_cast<size_t>(marker_hit));
                return true;
            }
        }

        if (try_toggle_datatip_at_nearest())
        {
            dispatch_series_selection_from_nearest();
            return true;
        }
        if (on_series_deselected_)
        {
            // Clicked on canvas but not near any series — deselect
            on_series_deselected_();
            return true;
        }
    }

    // Right click: remove marker
    if (button == 1)
    {
        if (active_axes3d_)
        {
            int idx = markers_.hit_test_3d(static_cast<float>(screen_x),
                                           static_cast<float>(screen_y),
                                           *active_axes3d_,
                                           10.0f);
            if (idx >= 0)
            {
                markers_.remove(static_cast<size_t>(idx));
                return true;
            }
        }
        else if (active_axes_)
        {
            int idx = markers_.hit_test(static_cast<float>(screen_x),
                                        static_cast<float>(screen_y),
                                        active_mapping(),
                                        10.0f,
                                        active_axes_);
            if (idx >= 0)
            {
                markers_.remove(static_cast<size_t>(idx));
                return true;
            }
        }
    }

    return false;
}

void DataInteraction::add_marker(double        data_x,
                                 double        data_y,
                                 const Series* series,
                                 size_t        index,
                                 const Axes*   axes)
{
    markers_.add(data_x, data_y, series, index, axes);
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
    region_.begin(screen_x, screen_y, AxisMapping(*active_axes_));
}

void DataInteraction::update_region_drag(double screen_x, double screen_y)
{
    // Use the axes where the ROI was started, not whatever the cursor is over now
    if (!region_axes_)
        return;
    region_.update_drag(screen_x, screen_y, AxisMapping(*region_axes_));
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
        // Map through the axis scale (log axes are linear in log(v)).
        const AxisMapping map(*axes_ptr);

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

            // x/y data are relative to the series offsets; limits are absolute.
            const double xoff     = series_ptr->x_offset();
            const double yoff     = series_ptr->y_offset();
            auto         drawable = [&](size_t i)
            {
                return map.x_drawable(static_cast<double>(x_data[i]) + xoff)
                       && map.y_drawable(static_cast<double>(y_data[i]) + yoff);
            };
            auto scr_x = [&](size_t i)
            { return map.to_screen_x(static_cast<double>(x_data[i]) + xoff); };
            auto scr_y = [&](size_t i)
            { return map.to_screen_y(static_cast<double>(y_data[i]) + yoff); };

            // Check if any data point maps to a screen position inside the rectangle
            bool hit = false;
            for (size_t i = 0; i < count && !hit; ++i)
            {
                if (!drawable(i))
                    continue;   // not drawn on a log axis
                const double px = scr_x(i);
                const double py = scr_y(i);
                if (px >= rx0 && px <= rx1 && py >= ry0 && py <= ry1)
                    hit = true;
            }

            // Also check line segment intersections with the rectangle edges
            if (!hit && count >= 2)
            {
                for (size_t i = 0; i + 1 < count && !hit; ++i)
                {
                    if (!drawable(i) || !drawable(i + 1))
                        continue;
                    double sx0 = scr_x(i);
                    double sy0 = scr_y(i);
                    double sx1 = scr_x(i + 1);
                    double sy1 = scr_y(i + 1);

                    // Cohen-Sutherland: if the segment can be clipped to the rectangle, it
                    // intersects
                    double cx0     = sx0;
                    double cy0     = sy0;
                    double cx1     = sx1;
                    double cy1     = sy1;
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
                        double px     = 0;
                        double py     = 0;
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

// ─── Annotation tool ────────────────────────────────────────────────────────

bool DataInteraction::on_mouse_click_annotate(int button, double screen_x, double screen_y)
{
    if (!active_axes_ || !last_figure_)
        return false;

    // Right click: remove annotation
    if (button == 1)
    {
        int idx = annotations_.hit_test(static_cast<float>(screen_x),
                                        static_cast<float>(screen_y),
                                        active_mapping(),
                                        10.0f,
                                        active_axes_);
        if (idx >= 0)
        {
            annotations_.remove(static_cast<size_t>(idx));
            return true;
        }
        return false;
    }

    // Left click
    if (button == 0)
    {
        // If currently editing an annotation, commit it first
        annotations_.cancel_editing();

        // Hit-test existing annotations — clicking on one starts editing
        int hit = annotations_.hit_test(static_cast<float>(screen_x),
                                        static_cast<float>(screen_y),
                                        active_mapping(),
                                        10.0f,
                                        active_axes_);
        if (hit >= 0)
        {
            // Re-enter editing mode on the clicked annotation
            auto& anns                             = annotations_.annotations_mut();
            anns[static_cast<size_t>(hit)].editing = true;
            return true;
        }

        // Place a new annotation at the click position (data coordinates,
        // through the axis scale so it lands under the cursor on log axes).
        const AxisMapping map = active_mapping();
        annotations_.add(static_cast<float>(map.to_data_x(screen_x)),
                         static_cast<float>(map.to_data_y(screen_y)),
                         active_axes_);
        return true;
    }

    return false;
}

void DataInteraction::begin_annotation_drag(double screen_x, double screen_y)
{
    if (!active_axes_)
        return;

    int hit = annotations_.hit_test(static_cast<float>(screen_x),
                                    static_cast<float>(screen_y),
                                    active_mapping(),
                                    10.0f,
                                    active_axes_);
    if (hit >= 0)
    {
        annotations_.begin_drag(static_cast<size_t>(hit),
                                static_cast<float>(screen_x),
                                static_cast<float>(screen_y));
    }
}

void DataInteraction::update_annotation_drag(double screen_x, double screen_y)
{
    annotations_.update_drag(static_cast<float>(screen_x), static_cast<float>(screen_y));
}

void DataInteraction::end_annotation_drag()
{
    annotations_.end_drag();
}

NearestPointResult DataInteraction::find_nearest(const CursorReadout& cursor, Figure& figure) const
{
    last_query_points_examined_ = 0;
    NearestPointResult best;
    best.found       = false;
    best.distance_px = std::numeric_limits<float>::max();
    float best_dist2 = std::numeric_limits<float>::max();

    auto cx = static_cast<float>(cursor.screen_x);
    auto cy = static_cast<float>(cursor.screen_y);

    // Pure-3D figures never set cursor.valid via 2D hit-test; still allow 3D pick
    // when the pointer is inside a 3D subplot viewport.
    bool over_plot = cursor.valid;
    if (!over_plot)
    {
        for (auto& axes_ptr : figure.all_axes())
        {
            if (!axes_ptr)
                continue;
            const auto& vp = axes_ptr->viewport();
            if (cx >= vp.x && cx <= vp.x + vp.w && cy >= vp.y && cy <= vp.y + vp.h)
            {
                over_plot = true;
                break;
            }
        }
    }
    if (!over_plot)
        return best;

    // 2D candidates.  Samples (vertices) win when within the snap radius;
    // otherwise a point on a line / contour segment within the radius; only
    // then an area under the cursor (bar, histogram bin, image cell).  The
    // closest vertex is still reported beyond the radius (tooltip fades out).
    const float snap_px    = tooltip_.snap_radius();
    const float snap2      = snap_px * snap_px;
    float       seg_dist2  = std::numeric_limits<float>::max();
    bool        area_found = false;
    // A segment beats a vertex inside the radius only when it is clearly
    // closer, so hovering near a sample keeps reporting that exact sample.
    constexpr float    SEGMENT_BIAS_PX = 3.0f;
    NearestPointResult seg_best;
    NearestPointResult area_best;

    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        const auto& vp = axes_ptr->viewport();

        // Only search axes the cursor is inside
        if (cx < vp.x || cx > vp.x + vp.w || cy < vp.y || cy > vp.y + vp.h)
            continue;

        // Project in axis space (log10/log2 for log axes) exactly like the
        // renderer, so the picked point is the one drawn under the cursor.
        const AxisMapping map(*axes_ptr);
        const ScaleType   xscale  = axes_ptr->xscale();
        const AxisLimits  xlim    = axis_space_limits(xscale, axes_ptr->x_limits());
        const double      x_range = axis_mapping::axis_span(xlim);
        const double      cur_dx  = map.to_data_x(cx);
        const double      cur_dy  = map.to_data_y(cy);

        // Snap window [lo, hi] in absolute data x around the cursor (the
        // inverse mapping is not symmetric about the cursor on a log axis).
        double win_lo = -std::numeric_limits<double>::infinity();
        double win_hi = std::numeric_limits<double>::infinity();
        if (vp.w > 0.0f)
        {
            const double cursor_ax_x = xlim.min + (static_cast<double>(cx - vp.x) / vp.w) * x_range;
            const double radius_ax   = std::abs(x_range) * static_cast<double>(snap_px) / vp.w;
            win_lo                   = from_axis_space(xscale, cursor_ax_x - radius_ax);
            win_hi                   = from_axis_space(xscale, cursor_ax_x + radius_ax);
            if (std::isnan(win_lo))
                win_lo = -std::numeric_limits<double>::infinity();
        }

        // Fill the shared fields of a 2D candidate.
        auto make_hit = [&](Series* s, int idx, HoverHitKind kind) -> NearestPointResult
        {
            NearestPointResult r;
            r.found        = true;
            r.series       = s;
            r.series_index = idx;
            r.axes         = axes_ptr.get();
            r.kind         = kind;
            return r;
        };

        // Screen position of an absolute data point (nullopt: not drawable).
        auto to_screen = [&](double ax_x, double ax_y) -> std::optional<ImVec2>
        {
            if (!std::isfinite(ax_x) || !std::isfinite(ax_y) || !map.x_drawable(ax_x)
                || !map.y_drawable(ax_y))
                return std::nullopt;
            return ImVec2(static_cast<float>(map.to_screen_x(ax_x)),
                          static_cast<float>(map.to_screen_y(ax_y)));
        };

        // Vertex + segment pass over samples [b, e) of a polyline given by
        // accessors; `segments` enables the between-samples hit.
        auto scan_polyline = [&](Series*      s,
                                 int          idx,
                                 size_t       b,
                                 size_t       e,
                                 auto&&       get_x,
                                 auto&&       get_y,
                                 bool         vertices,
                                 bool         segments,
                                 HoverHitKind seg_kind)
        {
            const double xoff = s->x_offset();
            const double yoff = s->y_offset();
            last_query_points_examined_ += e - b;
            std::optional<ImVec2> prev;
            double                prev_x = 0.0, prev_y = 0.0;
            for (size_t i = b; i < e; ++i)
            {
                const double abs_x = static_cast<double>(get_x(i)) + xoff;
                const double abs_y = static_cast<double>(get_y(i)) + yoff;
                const auto   p     = to_screen(abs_x, abs_y);
                if (p && vertices)
                {
                    const float dx    = cx - p->x;
                    const float dy    = cy - p->y;
                    const float dist2 = dx * dx + dy * dy;
                    if (dist2 < best_dist2)
                    {
                        best             = make_hit(s, idx, HoverHitKind::Vertex);
                        best.point_index = i;
                        best.data_x      = abs_x;
                        best.data_y      = abs_y;
                        best.screen_x    = p->x;
                        best.screen_y    = p->y;
                        best_dist2       = dist2;
                    }
                }
                if (p && prev && segments)
                {
                    const auto c = probe::closest_on_segment(prev->x, prev->y, p->x, p->y, cx, cy);
                    if (c.dist2 < seg_dist2 && c.dist2 <= snap2)
                    {
                        const auto sx        = static_cast<float>(prev->x + c.t * (p->x - prev->x));
                        const auto sy        = static_cast<float>(prev->y + c.t * (p->y - prev->y));
                        seg_best             = make_hit(s, idx, seg_kind);
                        seg_best.point_index = c.t < 0.5 ? i - 1 : i;
                        // Interpolate in axis space (what the renderer draws).
                        seg_best.data_x = map.to_data_x(sx);
                        seg_best.data_y = map.to_data_y(sy);
                        if (c.t <= 0.0 || c.t >= 1.0)
                        {
                            seg_best.data_x = c.t <= 0.0 ? prev_x : abs_x;
                            seg_best.data_y = c.t <= 0.0 ? prev_y : abs_y;
                        }
                        seg_best.screen_x = sx;
                        seg_best.screen_y = sy;
                        if (abs_x != prev_x)
                        {
                            seg_best.dy_dx =
                                static_cast<float>((abs_y - prev_y) / (abs_x - prev_x));
                            seg_best.dy_dx_valid = seg_kind == HoverHitKind::Segment;
                        }
                        seg_dist2 = static_cast<float>(c.dist2);
                    }
                }
                prev   = p;
                prev_x = abs_x;
                prev_y = abs_y;
            }
        };

        int series_counter = -1;
        for (auto& series_ptr : axes_ptr->series())
        {
            ++series_counter;
            if (!series_ptr || !series_ptr->visible())
                continue;
            Series* s = series_ptr.get();

            const float* x_data   = nullptr;
            const float* y_data   = nullptr;
            size_t       count    = 0;
            bool         x_sorted = false;
            bool         has_line = false;

            if (auto* ls = dynamic_cast<LineSeries*>(s))
            {
                x_data   = ls->x_data().data();
                y_data   = ls->y_data().data();
                count    = ls->point_count();
                x_sorted = ls->x_is_sorted();
                has_line = ls->line_style() != LineStyle::None;
            }
            else if (auto* sc = dynamic_cast<ScatterSeries*>(s))
            {
                x_data   = sc->x_data().data();
                y_data   = sc->y_data().data();
                count    = sc->point_count();
                x_sorted = sc->x_is_sorted();
            }
            else if (auto* cs = dynamic_cast<ChunkedLineSeries*>(s))
            {
                // Chunked storage: binary search the snap window in the
                // sorted samples, then scan just that window.
                const size_t                        chunks = cs->storage_chunk_count();
                std::vector<std::span<const float>> xs, ys;
                std::vector<size_t>                 starts;
                size_t                              total = 0;
                for (size_t c = 0; c < chunks; ++c)
                {
                    xs.push_back(cs->x_chunk(c));
                    ys.push_back(cs->y_chunk(c));
                    starts.push_back(total);
                    total += std::min(xs.back().size(), ys.back().size());
                }
                if (total == 0)
                    continue;
                auto locate = [&](size_t i)
                {
                    const auto it = std::upper_bound(starts.begin(), starts.end(), i);
                    const auto c  = static_cast<size_t>(it - starts.begin()) - 1;
                    return std::pair<size_t, size_t>{c, i - starts[c]};
                };
                auto get_x = [&](size_t i)
                {
                    const auto [c, k] = locate(i);
                    return xs[c][k];
                };
                auto get_y = [&](size_t i)
                {
                    const auto [c, k] = locate(i);
                    return ys[c][k];
                };
                const double xoff  = s->x_offset();
                auto         lower = [&](double v)
                {
                    size_t lo = 0, hi = total;
                    while (lo < hi)
                    {
                        const size_t mid = lo + (hi - lo) / 2;
                        if (static_cast<double>(get_x(mid)) < v)
                            lo = mid + 1;
                        else
                            hi = mid;
                    }
                    return lo;
                };
                size_t b = lower(win_lo - xoff);
                size_t e = lower(win_hi - xoff);
                if (b > 0)
                    --b;
                if (e < total)
                    ++e;
                // Very dense windows (zoomed far out): scan the samples
                // closest to the cursor only, the rest are sub-pixel.
                constexpr size_t MAX_CHUNKED_SCAN = size_t(1) << 18;
                if (e - b > MAX_CHUNKED_SCAN)
                {
                    const size_t mid = std::clamp(lower(cur_dx - xoff), b, e);
                    b = mid > b + MAX_CHUNKED_SCAN / 2 ? mid - MAX_CHUNKED_SCAN / 2 : b;
                    e = std::min(e, b + MAX_CHUNKED_SCAN);
                }
                scan_polyline(s,
                              series_counter,
                              b,
                              e,
                              get_x,
                              get_y,
                              true,
                              cs->line_style() != LineStyle::None,
                              HoverHitKind::Segment);
                continue;
            }
            else if (auto* st = dynamic_cast<StemSeries*>(s))
            {
                // Stem heads are the samples.
                x_data = st->stem_x().data();
                y_data = st->stem_y().data();
                count  = std::min(st->stem_x().size(), st->stem_y().size());
            }
            else if (auto* bs = dynamic_cast<BarSeries*>(s))
            {
                const double xoff = s->x_offset();
                const double yoff = s->y_offset();
                if (const auto bar = probe::bar_at(*bs, cur_dx - xoff, cur_dy - yoff))
                {
                    const double pos      = bs->bar_positions()[*bar];
                    const double h        = bs->bar_heights()[*bar];
                    const bool   vert     = bs->orientation() == BarOrientation::Vertical;
                    area_best             = make_hit(s, series_counter, HoverHitKind::Bar);
                    area_best.point_index = *bar;
                    area_best.data_x      = (vert ? pos : h) + xoff;
                    area_best.data_y      = (vert ? h : pos) + yoff;
                    area_best.value       = h;
                    area_best.has_value   = true;
                    area_found            = true;
                }
                continue;
            }
            else if (auto* hs = dynamic_cast<HistogramSeries*>(s))
            {
                const double xoff = s->x_offset();
                const double yoff = s->y_offset();
                if (const auto bin = probe::histogram_bin_at(*hs, cur_dx - xoff, cur_dy - yoff))
                {
                    const auto& e         = hs->bin_edges();
                    area_best             = make_hit(s, series_counter, HoverHitKind::Bin);
                    area_best.point_index = *bin;
                    area_best.range_lo    = e[*bin] + xoff;
                    area_best.range_hi    = e[*bin + 1] + xoff;
                    area_best.data_x      = (area_best.range_lo + area_best.range_hi) * 0.5;
                    area_best.value       = hs->bin_counts()[*bin];
                    area_best.data_y      = area_best.value + yoff;
                    area_best.has_value   = true;
                    area_found            = true;
                }
                continue;
            }
            else if (auto* img = dynamic_cast<ImageSeries*>(s))
            {
                size_t col = 0, row = 0;
                if (!img->empty() && img->cell_at(cur_dx, cur_dy, col, row))
                {
                    area_best             = make_hit(s, series_counter, HoverHitKind::Pixel);
                    area_best.point_index = row * img->cols() + col;
                    area_best.cell_col    = col;
                    area_best.cell_row    = row;
                    img->cell_center(col, row, area_best.data_x, area_best.data_y);
                    if (const auto v = img->value_at(cur_dx, cur_dy))
                    {
                        area_best.value     = *v;
                        area_best.has_value = std::isfinite(*v);
                    }
                    area_found = true;
                }
                continue;
            }
            else if (auto* ct = dynamic_cast<ContourSeries*>(s))
            {
                const auto xs = ct->x_data();
                const auto ys = ct->y_data();
                scan_polyline(
                    s,
                    series_counter,
                    0,
                    std::min(xs.size(), ys.size()),
                    [&](size_t i) { return xs[i]; },
                    [&](size_t i) { return ys[i]; },
                    false,
                    true,
                    HoverHitKind::Contour);
                continue;
            }

            if (!x_data || !y_data || count == 0)
                continue;

            // x/y data are relative to the series offsets; limits are absolute.
            const double xoff = s->x_offset();

            size_t query_begin = 0;
            size_t query_end   = count;

            // For monotonic X data, only points (and segments) around the
            // snap window can be interactable.  Binary search that range,
            // keeping one neighbour on either side so segments crossing the
            // window and nearest-point reporting just outside it still work.
            // Unsorted data retains the full scan.
            if (x_sorted && count > 64 && vp.w > 0.0f)
            {
                const auto q = probe::sorted_window(std::span<const float>(x_data, count),
                                                    win_lo - xoff,
                                                    win_hi - xoff);
                query_begin  = q.begin;
                query_end    = q.end;
            }

            scan_polyline(
                s,
                series_counter,
                query_begin,
                query_end,
                [&](size_t i) { return x_data[i]; },
                [&](size_t i) { return y_data[i]; },
                true,
                has_line,
                HoverHitKind::Segment);
        }
    }

    // Pick between the vertex, segment and area candidates.
    if (seg_best.found && seg_dist2 <= snap2)
    {
        const float vd = best.found ? std::sqrt(best_dist2) : std::numeric_limits<float>::max();
        const float sd = std::sqrt(seg_dist2);
        if (!(best.found && best_dist2 <= snap2) || sd + SEGMENT_BIAS_PX < vd)
        {
            best       = seg_best;
            best_dist2 = seg_dist2;
        }
    }
    if (area_found && !(best.found && best_dist2 <= snap2))
    {
        // The cursor is inside the area: anchor the tooltip at the cursor-side
        // representative point (bar top / bin top / cell centre).
        best = area_best;
        const AxisMapping amap(*area_best.axes);
        best.screen_x = static_cast<float>(amap.to_screen_x(best.data_x));
        best.screen_y = static_cast<float>(amap.to_screen_y(best.data_y));
        // Image cells can be large: keep the readout at the cursor.
        if (best.kind == HoverHitKind::Pixel || !std::isfinite(best.screen_x)
            || !std::isfinite(best.screen_y))
        {
            best.screen_x = cx;
            best.screen_y = cy;
        }
        best_dist2 = 0.0f;
    }

    if (best.found)
        best.distance_px = std::sqrt(best_dist2);

    // Search 3D axes (stored in all_axes_, not axes_)
    for (auto& axes_ptr : figure.all_axes())
    {
        if (!axes_ptr)
            continue;
        auto* axes3d = dynamic_cast<Axes3D*>(axes_ptr.get());
        if (!axes3d)
            continue;

        const Nearest3DPickCandidate pick = find_nearest_3d_in_axes(*axes3d, cx, cy);
        if (!pick.found)
            continue;

        if (!best.found || pick.distance_px < best.distance_px)
        {
            best.found        = true;
            best.series       = pick.series;
            best.series_index = -1;
            for (size_t si = 0; si < axes3d->series().size(); ++si)
            {
                if (axes3d->series()[si].get() == pick.series)
                {
                    best.series_index = static_cast<int>(si);
                    break;
                }
            }
            best.point_index = pick.point_index;
            best.data_x      = pick.data_x;
            best.data_y      = pick.data_y;
            best.data_z      = pick.data_z;
            best.screen_x    = pick.screen_x;
            best.screen_y    = pick.screen_y;
            best.distance_px = pick.distance_px;
            best.ndc_depth   = pick.ndc_depth;
            best.is_3d       = true;
            best.axes3d      = axes3d;
            best.axes        = nullptr;
            best.kind        = HoverHitKind::Vertex;
            best.has_value   = false;
            best.dy_dx_valid = false;
            best.dy_dx       = 0.0f;
        }
    }

    // Contour hit: report the level of the line under the cursor.
    if (best.found && best.kind == HoverHitKind::Contour)
    {
        if (const auto* ct = dynamic_cast<const ContourSeries*>(best.series))
        {
            if (const auto level = probe::contour_level_for_point(*ct, best.point_index))
            {
                best.value     = *level;
                best.has_value = true;
            }
        }
    }

    // Compute dy/dx at the nearest point via finite difference (2D samples only)
    if (best.found && !best.is_3d && best.series && best.kind == HoverHitKind::Vertex)
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

// ─── Overlay snapshot ────────────────────────────────────────────────────────

OverlaySnapshot DataInteraction::capture_overlay_snapshot(const Figure& figure) const
{
    OverlaySnapshot snap;
    snap.crosshair_enabled = crosshair_.enabled();
    snap.tooltip_enabled   = tooltip_.enabled();

    // Helper: resolve an Axes pointer to its index in the figure
    auto resolve_axes_index = [&](const Axes* axes) -> size_t
    {
        if (!axes)
            return 0;
        size_t idx = 0;
        for (const auto& ax : figure.axes())
        {
            if (ax.get() == axes)
                return idx;
            ++idx;
        }
        return 0;
    };

    // Markers
    for (const auto& m : markers_.markers())
    {
        OverlaySnapshot::MarkerEntry me;
        me.data_x       = m.data_x;
        me.data_y       = m.data_y;
        me.series_label = m.series_label;
        me.point_index  = m.point_index;
        me.axes_index   = resolve_axes_index(m.axes);
        snap.markers.push_back(std::move(me));
    }

    // Annotations
    for (const auto& ann : annotations_.annotations())
    {
        if (ann.editing || ann.text.empty())
            continue;
        OverlaySnapshot::AnnotationEntry ae;
        ae.data_x     = ann.data_x;
        ae.data_y     = ann.data_y;
        ae.text       = ann.text;
        ae.color      = ann.color;
        ae.offset_x   = ann.offset_x;
        ae.offset_y   = ann.offset_y;
        ae.axes_index = resolve_axes_index(ann.axes);
        snap.annotations.push_back(std::move(ae));
    }

    return snap;
}

void DataInteraction::restore_overlay_snapshot(const OverlaySnapshot& snapshot, Figure& figure)
{
    set_crosshair(snapshot.crosshair_enabled);
    set_tooltip(snapshot.tooltip_enabled);

    // Helper: resolve an axes index to a pointer
    auto resolve_axes_ptr = [&](size_t idx) -> const Axes*
    {
        if (idx < figure.axes().size() && figure.axes()[idx])
            return figure.axes()[idx].get();
        return nullptr;
    };

    // Restore markers
    clear_markers();
    for (const auto& me : snapshot.markers)
    {
        // Find the series by label to get a valid pointer
        const Series* series_ptr = nullptr;
        const Axes*   axes_ptr   = resolve_axes_ptr(me.axes_index);
        if (axes_ptr)
        {
            for (const auto& sp : axes_ptr->series())
            {
                if (sp->label() == me.series_label)
                {
                    series_ptr = sp.get();
                    break;
                }
            }
        }
        else
        {
            // Search all axes for the series
            for (const auto& ax : figure.axes())
            {
                if (!ax)
                    continue;
                for (const auto& sp : ax->series())
                {
                    if (sp->label() == me.series_label)
                    {
                        series_ptr = sp.get();
                        axes_ptr   = ax.get();
                        break;
                    }
                }
                if (series_ptr)
                    break;
            }
        }
        add_marker(me.data_x, me.data_y, series_ptr, me.point_index, axes_ptr);
    }

    // Restore annotations
    annotations_.clear();
    for (const auto& ae : snapshot.annotations)
    {
        if (ae.text.empty())
            continue;
        const Axes* axes_ptr = resolve_axes_ptr(ae.axes_index);
        size_t      idx      = annotations_.add(ae.data_x, ae.data_y, axes_ptr);
        auto&       ann      = annotations_.annotations_mut()[idx];
        ann.text             = ae.text;
        ann.color            = ae.color;
        ann.offset_x         = ae.offset_x;
        ann.offset_y         = ae.offset_y;
        ann.editing          = false;
    }
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
