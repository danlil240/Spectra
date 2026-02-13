#ifdef PLOTIX_USE_IMGUI

#include "data_interaction.hpp"

#include <plotix/axes.hpp>
#include <plotix/figure.hpp>
#include <plotix/series.hpp>

#include <cmath>
#include <limits>

namespace plotix {

void DataInteraction::set_fonts(ImFont* body, ImFont* heading) {
    tooltip_.set_fonts(body, heading);
}

void DataInteraction::update(const CursorReadout& cursor, Figure& figure) {
    last_cursor_ = cursor;

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

    // Run nearest-point query
    nearest_ = find_nearest(cursor, figure);
}

void DataInteraction::draw_overlays(float window_width, float window_height) {
    // Draw markers for all axes that have them
    if (active_axes_) {
        markers_.draw(active_viewport_,
                      xlim_min_, xlim_max_, ylim_min_, ylim_max_);
    }

    // Draw crosshair using the raw (unsnapped) cursor position
    if (active_axes_) {
        crosshair_.draw(last_cursor_, active_viewport_,
                        xlim_min_, xlim_max_, ylim_min_, ylim_max_);
    }

    // Draw tooltip last (on top)
    tooltip_.draw(nearest_, window_width, window_height);
}

bool DataInteraction::on_mouse_click(int button, double screen_x, double screen_y) {
    if (!active_axes_) return false;

    // Left click: pin a marker at the nearest point
    if (button == 0 && nearest_.found && nearest_.distance_px <= tooltip_.snap_radius()) {
        markers_.add(nearest_.data_x, nearest_.data_y, nearest_.series, nearest_.point_index);
        return true;
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
