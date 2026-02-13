#pragma once

#include "undo_manager.hpp"

#include <plotix/axes.hpp>
#include <plotix/color.hpp>
#include <plotix/figure.hpp>
#include <plotix/series.hpp>

#include <functional>
#include <string>
#include <vector>

namespace plotix {

// ─── Convenience helpers for undoable property mutations ─────────────────────
// These capture before/after state and push to UndoManager in a single call.
// All helpers are safe to call with a null UndoManager pointer (no-op).

// Generic: push a value change with a setter lambda
template <typename T>
void undoable_set(UndoManager* mgr, const std::string& description,
                  T before, T after, std::function<void(const T&)> setter) {
    if (setter) setter(after);
    if (mgr) mgr->push_value<T>(description, before, after, std::move(setter));
}

// ─── Axis limits ─────────────────────────────────────────────────────────────

inline void undoable_xlim(UndoManager* mgr, Axes& ax, float new_min, float new_max) {
    auto old = ax.x_limits();
    ax.xlim(new_min, new_max);
    if (mgr) {
        Axes* ptr = &ax;
        AxisLimits before = old;
        AxisLimits after{new_min, new_max};
        mgr->push(UndoAction{
            "Change X limits",
            [ptr, before]() { ptr->xlim(before.min, before.max); },
            [ptr, after]()  { ptr->xlim(after.min, after.max); }
        });
    }
}

inline void undoable_ylim(UndoManager* mgr, Axes& ax, float new_min, float new_max) {
    auto old = ax.y_limits();
    ax.ylim(new_min, new_max);
    if (mgr) {
        Axes* ptr = &ax;
        AxisLimits before = old;
        AxisLimits after{new_min, new_max};
        mgr->push(UndoAction{
            "Change Y limits",
            [ptr, before]() { ptr->ylim(before.min, before.max); },
            [ptr, after]()  { ptr->ylim(after.min, after.max); }
        });
    }
}

inline void undoable_set_limits(UndoManager* mgr, Axes& ax,
                                 AxisLimits new_x, AxisLimits new_y) {
    auto old_x = ax.x_limits();
    auto old_y = ax.y_limits();
    ax.xlim(new_x.min, new_x.max);
    ax.ylim(new_y.min, new_y.max);
    if (mgr) {
        Axes* ptr = &ax;
        mgr->push(UndoAction{
            "Change axis limits",
            [ptr, old_x, old_y]() {
                ptr->xlim(old_x.min, old_x.max);
                ptr->ylim(old_y.min, old_y.max);
            },
            [ptr, new_x, new_y]() {
                ptr->xlim(new_x.min, new_x.max);
                ptr->ylim(new_y.min, new_y.max);
            }
        });
    }
}

// ─── Grid toggle ─────────────────────────────────────────────────────────────

inline void undoable_toggle_grid(UndoManager* mgr, Axes& ax) {
    bool old_val = ax.grid_enabled();
    bool new_val = !old_val;
    ax.set_grid_enabled(new_val);
    if (mgr) {
        Axes* ptr = &ax;
        mgr->push(UndoAction{
            new_val ? "Show grid" : "Hide grid",
            [ptr, old_val]() { ptr->set_grid_enabled(old_val); },
            [ptr, new_val]() { ptr->set_grid_enabled(new_val); }
        });
    }
}

// ─── Border toggle ──────────────────────────────────────────────────────────

inline void undoable_toggle_border(UndoManager* mgr, Axes& ax) {
    bool old_val = ax.border_enabled();
    bool new_val = !old_val;
    ax.set_border_enabled(new_val);
    if (mgr) {
        Axes* ptr = &ax;
        mgr->push(UndoAction{
            new_val ? "Show border" : "Hide border",
            [ptr, old_val]() { ptr->set_border_enabled(old_val); },
            [ptr, new_val]() { ptr->set_border_enabled(new_val); }
        });
    }
}

// ─── Series visibility ──────────────────────────────────────────────────────

inline void undoable_toggle_series_visibility(UndoManager* mgr, Series& s) {
    bool old_val = s.visible();
    bool new_val = !old_val;
    s.visible(new_val);
    if (mgr) {
        Series* ptr = &s;
        std::string name = s.label().empty() ? "series" : s.label();
        mgr->push(UndoAction{
            new_val ? "Show " + name : "Hide " + name,
            [ptr, old_val]() { ptr->visible(old_val); },
            [ptr, new_val]() { ptr->visible(new_val); }
        });
    }
}

// ─── Series color ────────────────────────────────────────────────────────────

inline void undoable_set_series_color(UndoManager* mgr, Series& s, const Color& new_color) {
    Color old_color = s.color();
    s.set_color(new_color);
    if (mgr) {
        Series* ptr = &s;
        mgr->push(UndoAction{
            "Change color of " + (s.label().empty() ? "series" : s.label()),
            [ptr, old_color]() { ptr->set_color(old_color); },
            [ptr, new_color]() { ptr->set_color(new_color); }
        });
    }
}

// ─── Line width ──────────────────────────────────────────────────────────────

inline void undoable_set_line_width(UndoManager* mgr, LineSeries& ls, float new_width) {
    float old_width = ls.width();
    ls.width(new_width);
    if (mgr) {
        LineSeries* ptr = &ls;
        mgr->push(UndoAction{
            "Change line width",
            [ptr, old_width]() { ptr->width(old_width); },
            [ptr, new_width]() { ptr->width(new_width); }
        });
    }
}

// ─── Marker size ─────────────────────────────────────────────────────────────

inline void undoable_set_marker_size(UndoManager* mgr, ScatterSeries& sc, float new_size) {
    float old_size = sc.size();
    sc.size(new_size);
    if (mgr) {
        ScatterSeries* ptr = &sc;
        mgr->push(UndoAction{
            "Change marker size",
            [ptr, old_size]() { ptr->size(old_size); },
            [ptr, new_size]() { ptr->size(new_size); }
        });
    }
}

// ─── Legend visibility ───────────────────────────────────────────────────────

inline void undoable_toggle_legend(UndoManager* mgr, Figure& fig) {
    bool old_val = fig.legend().visible;
    bool new_val = !old_val;
    fig.legend().visible = new_val;
    if (mgr) {
        Figure* ptr = &fig;
        mgr->push(UndoAction{
            new_val ? "Show legend" : "Hide legend",
            [ptr, old_val]() { ptr->legend().visible = old_val; },
            [ptr, new_val]() { ptr->legend().visible = new_val; }
        });
    }
}

// ─── Axis title / labels ────────────────────────────────────────────────────

inline void undoable_set_title(UndoManager* mgr, Axes& ax, const std::string& new_title) {
    std::string old_title = ax.get_title();
    ax.title(new_title);
    if (mgr) {
        Axes* ptr = &ax;
        mgr->push(UndoAction{
            "Change title",
            [ptr, old_title]() { ptr->title(old_title); },
            [ptr, new_title]() { ptr->title(new_title); }
        });
    }
}

inline void undoable_set_xlabel(UndoManager* mgr, Axes& ax, const std::string& new_label) {
    std::string old_label = ax.get_xlabel();
    ax.xlabel(new_label);
    if (mgr) {
        Axes* ptr = &ax;
        mgr->push(UndoAction{
            "Change X label",
            [ptr, old_label]() { ptr->xlabel(old_label); },
            [ptr, new_label]() { ptr->xlabel(new_label); }
        });
    }
}

inline void undoable_set_ylabel(UndoManager* mgr, Axes& ax, const std::string& new_label) {
    std::string old_label = ax.get_ylabel();
    ax.ylabel(new_label);
    if (mgr) {
        Axes* ptr = &ax;
        mgr->push(UndoAction{
            "Change Y label",
            [ptr, old_label]() { ptr->ylabel(old_label); },
            [ptr, new_label]() { ptr->ylabel(new_label); }
        });
    }
}

// ─── Grouped multi-axes operations ──────────────────────────────────────────

// Toggle grid on all axes in a figure as a single undo action
inline void undoable_toggle_grid_all(UndoManager* mgr, Figure& fig) {
    if (mgr) mgr->begin_group("Toggle grid");
    for (auto& ax : fig.axes_mut()) {
        if (ax) undoable_toggle_grid(mgr, *ax);
    }
    if (mgr) mgr->end_group();
}

// Toggle border on all axes in a figure as a single undo action
inline void undoable_toggle_border_all(UndoManager* mgr, Figure& fig) {
    if (mgr) mgr->begin_group("Toggle border");
    for (auto& ax : fig.axes_mut()) {
        if (ax) undoable_toggle_border(mgr, *ax);
    }
    if (mgr) mgr->end_group();
}

// Capture full figure axis state for undo (e.g., before auto-fit / reset view)
struct FigureAxisSnapshot {
    struct Entry {
        Axes* axes;
        AxisLimits x_limits;
        AxisLimits y_limits;
    };
    std::vector<Entry> entries;
};

inline FigureAxisSnapshot capture_figure_axes(Figure& fig) {
    FigureAxisSnapshot snap;
    for (auto& ax : fig.axes_mut()) {
        if (ax) {
            snap.entries.push_back({ax.get(), ax->x_limits(), ax->y_limits()});
        }
    }
    return snap;
}

inline void restore_figure_axes(const FigureAxisSnapshot& snap) {
    for (auto& e : snap.entries) {
        if (e.axes) {
            e.axes->xlim(e.x_limits.min, e.x_limits.max);
            e.axes->ylim(e.y_limits.min, e.y_limits.max);
        }
    }
}

inline void undoable_reset_view(UndoManager* mgr, Figure& fig) {
    auto before = capture_figure_axes(fig);
    for (auto& ax : fig.axes_mut()) {
        if (ax) ax->auto_fit();
    }
    auto after = capture_figure_axes(fig);
    if (mgr) {
        mgr->push(UndoAction{
            "Reset view",
            [before]() { restore_figure_axes(before); },
            [after]()  { restore_figure_axes(after); }
        });
    }
}

} // namespace plotix
