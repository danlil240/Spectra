#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <spectra/series.hpp>

struct ImDrawList;

namespace spectra::ui
{
class ThemeManager;
}   // namespace spectra::ui

namespace spectra
{

class Figure;
class AxisLinkManager;
struct CursorReadout;

// Crosshair overlay: renders dashed horizontal and vertical lines
// through the cursor position, clipped to the axes viewport.
// Also draws axis-intersection labels showing the X and Y values.
class Crosshair
{
   public:
    Crosshair() = default;

    void set_enabled(bool e) { enabled_ = e; }
    bool enabled() const { return enabled_; }
    void toggle() { enabled_ = !enabled_; }

    // Draw crosshair lines and axis labels for the given cursor position.
    // viewport is the axes Rect in screen coordinates.
    // When dl is non-null, draws into the given draw list instead of GetForegroundDrawList().
    void draw(const CursorReadout& cursor,
              const Rect&          viewport,
              float                xlim_min,
              float                xlim_max,
              float                ylim_min,
              float                ylim_max,
              ImDrawList*          dl = nullptr);

    // Draw crosshair across ALL subplots in the figure.
    // The vertical line is drawn at the same data-X on every axes.
    // The horizontal line is drawn at the same normalized Y position
    // on every axes, with each subplot showing its own data-Y label.
    void draw_all_axes(const CursorReadout& cursor,
                       Figure&              figure,
                       AxisLinkManager*     link_mgr = nullptr,
                       ImDrawList*          dl       = nullptr);

    // Configuration
    void set_dash_length(float px) { dash_length_ = px; }
    void set_gap_length(float px) { gap_length_ = px; }
    void set_theme_manager(ui::ThemeManager* tm) { theme_mgr_ = tm; }

   private:
    ui::ThemeManager* theme_mgr_ = nullptr;
    bool  enabled_     = false;
    float dash_length_ = 6.0f;
    float gap_length_  = 4.0f;
    float opacity_     = 0.0f;
};

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
