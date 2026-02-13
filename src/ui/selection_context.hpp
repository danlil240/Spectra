#pragma once

#include <plotix/fwd.hpp>

namespace plotix::ui {

enum class SelectionType { None, Figure, Axes, Series };

struct SelectionContext {
    SelectionType type = SelectionType::None;
    Figure* figure = nullptr;
    Axes*   axes   = nullptr;
    Series* series = nullptr;
    int     axes_index  = -1;  // Which axes in the figure (for display)
    int     series_index = -1; // Which series in the axes (for display)

    void clear() {
        type = SelectionType::None;
        figure = nullptr;
        axes = nullptr;
        series = nullptr;
        axes_index = -1;
        series_index = -1;
    }

    void select_figure(Figure* fig) {
        clear();
        type = SelectionType::Figure;
        figure = fig;
    }

    void select_axes(Figure* fig, Axes* ax, int idx) {
        clear();
        type = SelectionType::Axes;
        figure = fig;
        axes = ax;
        axes_index = idx;
    }

    void select_series(Figure* fig, Axes* ax, int ax_idx, Series* s, int s_idx) {
        clear();
        type = SelectionType::Series;
        figure = fig;
        axes = ax;
        axes_index = ax_idx;
        series = s;
        series_index = s_idx;
    }
};

} // namespace plotix::ui
