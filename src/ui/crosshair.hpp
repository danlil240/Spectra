#pragma once

#ifdef PLOTIX_USE_IMGUI

#include <plotix/series.hpp>

namespace plotix {

class Figure;
struct CursorReadout;

// Crosshair overlay: renders dashed horizontal and vertical lines
// through the cursor position, clipped to the axes viewport.
// Also draws axis-intersection labels showing the X and Y values.
class Crosshair {
public:
    Crosshair() = default;

    void set_enabled(bool e) { enabled_ = e; }
    bool enabled() const { return enabled_; }
    void toggle() { enabled_ = !enabled_; }

    // Draw crosshair lines and axis labels for the given cursor position.
    // viewport is the axes Rect in screen coordinates.
    void draw(const CursorReadout& cursor, const Rect& viewport,
              float xlim_min, float xlim_max, float ylim_min, float ylim_max);

    // Draw crosshair across ALL subplots in the figure.
    // The vertical line is drawn at the same data-X on every axes;
    // the horizontal line is only drawn on the axes the cursor is over.
    void draw_all_axes(const CursorReadout& cursor, Figure& figure);

    // Configuration
    void set_dash_length(float px) { dash_length_ = px; }
    void set_gap_length(float px) { gap_length_ = px; }

private:
    bool enabled_ = false;
    float dash_length_ = 6.0f;
    float gap_length_ = 4.0f;
    float opacity_ = 0.0f;
};

} // namespace plotix

#endif // PLOTIX_USE_IMGUI
