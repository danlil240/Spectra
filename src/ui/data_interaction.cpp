#ifdef PLOTIX_USE_IMGUI

#include "data_interaction.hpp"
#include "axis_link.hpp"

#include <plotix/axes.hpp>
#include <plotix/figure.hpp>
#include <plotix/series.hpp>

#include <imgui.h>
#include <cmath>
#include <limits>

namespace plotix {

void DataInteraction::set_fonts(ImFont* body, ImFont* heading, ImFont* icon) {
    tooltip_.set_fonts(body, heading);
    region_.set_fonts(body, heading);
    legend_.set_fonts(body, icon);
}

void DataInteraction::set_transition_engine(TransitionEngine* te) {
    region_.set_transition_engine(te);
    legend_.set_transition_engine(te);
}

void DataInteraction::update(const CursorReadout& cursor, Figure& figure) {
    last_cursor_ = cursor;
    last_figure_ = &figure;

    // Update legend animation state
    float dt = 0.016f; // fallback
#ifdef PLOTIX_USE_IMGUI
    dt = ImGui::GetIO().DeltaTime;
#endif
    legend_.update(dt, figure);

    // Determine which axes the cursor is over by hit-testing viewports
    active_axes_ = nullptr;
    for (auto& axes_ptr : figure.axes()) {
        if (!axes_ptr) continue;
        const auto& vp = axes_ptr->viewport();
        float sx = static_cast<float>(cursor.screen_x);
        float sy = static_cast<float>(cursor.screen_y);
        if (cursor.valid &&
            sx >= vp.x && sx <= vp.x + vp.w &&
            sy >= vp.y && sy <= vp.y + vp.h) {
            active_axes_ = axes_ptr.get();
            active_viewport_ = vp;
            auto xl = axes_ptr->x_limits();
            auto yl = axes_ptr->y_limits();
            xlim_min_ = xl.min; xlim_max_ = xl.max;
            ylim_min_ = yl.min; ylim_max_ = yl.max;
            break;
        }
    }

    // Broadcast shared cursor to linked axes
    if (axis_link_mgr_ && active_axes_ && cursor.valid) {
        SharedCursor sc;
        sc.valid = true;
        sc.data_x = xlim_min_ + (static_cast<float>(cursor.screen_x) - active_viewport_.x) / active_viewport_.w * (xlim_max_ - xlim_min_);
        sc.data_y = ylim_max_ - (static_cast<float>(cursor.screen_y) - active_viewport_.y) / active_viewport_.h * (ylim_max_ - ylim_min_);
        sc.screen_x = cursor.screen_x;
        sc.screen_y = cursor.screen_y;
        sc.source_axes = active_axes_;
        axis_link_mgr_->update_shared_cursor(sc);
    } else if (axis_link_mgr_) {
        axis_link_mgr_->clear_shared_cursor();
    }

    // Run nearest-point query
    nearest_ = find_nearest(cursor, figure);
}

void DataInteraction::draw_overlays(float window_width, float window_height) {
    // Draw legend interaction for each axes
    if (last_figure_) {
        size_t idx = 0;
        for (auto& axes_ptr : last_figure_->axes_mut()) {
            if (axes_ptr) {
                legend_.draw(*axes_ptr, axes_ptr->viewport(), idx);
            }
            ++idx;
        }
    }

    // Draw markers for all axes that have them
    if (active_axes_) {
        markers_.draw(active_viewport_,
                      xlim_min_, xlim_max_, ylim_min_, ylim_max_);
    }

    // Draw region selection overlay
    if (active_axes_) {
        region_.draw(active_viewport_,
                     xlim_min_, xlim_max_, ylim_min_, ylim_max_,
                     window_width, window_height);
    }

    // Draw crosshair: use multi-axes mode if figure has multiple axes
    if (last_figure_ && last_figure_->axes().size() > 1) {
        crosshair_.draw_all_axes(last_cursor_, *last_figure_, axis_link_mgr_);
    } else if (active_axes_) {
        crosshair_.draw(last_cursor_, active_viewport_,
                        xlim_min_, xlim_max_, ylim_min_, ylim_max_);
    }

    // Draw tooltip last (on top)
    tooltip_.draw(nearest_, window_width, window_height);
}

bool DataInteraction::on_mouse_click(int button, double screen_x, double screen_y) {
    if (!active_axes_ || !last_figure_) return false;

    // Left click: select the nearest series (for inspector editing)
    if (button == 0 && nearest_.found) {
        // Use a generous snap radius for series selection — the user is clicking
        // on lines/curves, not individual data points.  30px feels natural.
        constexpr float SELECT_SNAP_PX = 30.0f;
        if (nearest_.distance_px <= SELECT_SNAP_PX && on_series_selected_) {
            // Find axes index and series index for the callback
            int ax_idx = 0;
            for (auto& axes_ptr : last_figure_->axes()) {
                if (!axes_ptr) { ax_idx++; continue; }
                int s_idx = 0;
                for (auto& series_ptr : axes_ptr->series()) {
                    if (series_ptr.get() == nearest_.series) {
                        on_series_selected_(last_figure_, axes_ptr.get(),
                                            ax_idx, series_ptr.get(), s_idx);
                        return true;
                    }
                    s_idx++;
                }
                ax_idx++;
            }
        }
    }

    // Right click: remove a marker if clicking near one
    if (button == 1) {
        int idx = markers_.hit_test(
            static_cast<float>(screen_x), static_cast<float>(screen_y),
            active_viewport_, xlim_min_, xlim_max_, ylim_min_, ylim_max_);
        if (idx >= 0) {
            markers_.remove(static_cast<size_t>(idx));
            return true;
        }
    }

    return false;
}

void DataInteraction::add_marker(float data_x, float data_y, const Series* series, size_t index) {
    markers_.add(data_x, data_y, series, index);
}

void DataInteraction::remove_marker(size_t idx) {
    markers_.remove(idx);
}

void DataInteraction::clear_markers() {
    markers_.clear();
}

void DataInteraction::set_snap_radius(float px) {
    tooltip_.set_snap_radius(px);
}

// ─── Region selection ───────────────────────────────────────────────────────

void DataInteraction::begin_region_select(double screen_x, double screen_y) {
    if (!active_axes_) return;
    region_.begin(screen_x, screen_y, active_viewport_,
                  xlim_min_, xlim_max_, ylim_min_, ylim_max_);
}

void DataInteraction::update_region_drag(double screen_x, double screen_y) {
    if (!active_axes_) return;
    region_.update_drag(screen_x, screen_y, active_viewport_,
                        xlim_min_, xlim_max_, ylim_min_, ylim_max_);
}

void DataInteraction::finish_region_select() {
    region_.finish(active_axes_);
}

void DataInteraction::dismiss_region_select() {
    region_.dismiss();
}

NearestPointResult DataInteraction::find_nearest(const CursorReadout& cursor, Figure& figure) const {
    NearestPointResult best;
    best.found = false;
    best.distance_px = std::numeric_limits<float>::max();

    if (!cursor.valid) return best;

    float cx = static_cast<float>(cursor.screen_x);
    float cy = static_cast<float>(cursor.screen_y);

    // Search all axes
    for (auto& axes_ptr : figure.axes()) {
        if (!axes_ptr) continue;
        const auto& vp = axes_ptr->viewport();

        // Only search axes the cursor is inside
        if (cx < vp.x || cx > vp.x + vp.w ||
            cy < vp.y || cy > vp.y + vp.h)
            continue;

        auto xlim = axes_ptr->x_limits();
        auto ylim = axes_ptr->y_limits();
        float x_range = xlim.max - xlim.min;
        float y_range = ylim.max - ylim.min;
        if (x_range == 0.0f) x_range = 1.0f;
        if (y_range == 0.0f) y_range = 1.0f;

        for (auto& series_ptr : axes_ptr->series()) {
            if (!series_ptr || !series_ptr->visible()) continue;

            const float* x_data = nullptr;
            const float* y_data = nullptr;
            size_t count = 0;

            if (auto* ls = dynamic_cast<LineSeries*>(series_ptr.get())) {
                x_data = ls->x_data().data();
                y_data = ls->y_data().data();
                count = ls->point_count();
            } else if (auto* sc = dynamic_cast<ScatterSeries*>(series_ptr.get())) {
                x_data = sc->x_data().data();
                y_data = sc->y_data().data();
                count = sc->point_count();
            }

            if (!x_data || !y_data || count == 0) continue;

            // Linear scan for nearest point (screen-space distance)
            for (size_t i = 0; i < count; ++i) {
                // Convert data point to screen coordinates
                float norm_x = (x_data[i] - xlim.min) / x_range;
                float norm_y = (y_data[i] - ylim.min) / y_range;
                float sx = vp.x + norm_x * vp.w;
                float sy = vp.y + (1.0f - norm_y) * vp.h;

                float dx = cx - sx;
                float dy = cy - sy;
                float dist = std::sqrt(dx * dx + dy * dy);

                if (dist < best.distance_px) {
                    best.found = true;
                    best.series = series_ptr.get();
                    best.point_index = i;
                    best.data_x = x_data[i];
                    best.data_y = y_data[i];
                    best.screen_x = sx;
                    best.screen_y = sy;
                    best.distance_px = dist;
                }
            }
        }
    }

    return best;
}

} // namespace plotix

#endif // PLOTIX_USE_IMGUI
