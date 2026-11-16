#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include "series_probe.hpp"

struct ImDrawList;
struct ImFont;

namespace spectra::ui
{
class ThemeManager;
}   // namespace spectra::ui

namespace spectra
{

class Axes;
class Figure;
class AxisLinkManager;
struct CursorReadout;

// "Compare at X" readout: while the crosshair is on, one compact tooltip lists
// every visible series of the hovered axes with its colour swatch, name and
// value at the cursor X (interpolated or nearest sample), and each series'
// point is marked on the plot.  Axes X-linked to the hovered one get the same
// markers and a readout at the same X.
class CompareReadout
{
   public:
    void set_theme_manager(ui::ThemeManager* tm) { theme_mgr_ = tm; }
    void set_fonts(ImFont* body) { font_body_ = body; }

    void set_enabled(bool e) { enabled_ = e; }
    bool enabled() const { return enabled_; }

    void             set_mode(probe::ProbeMode m) { mode_ = m; }
    probe::ProbeMode mode() const { return mode_; }

    void               set_sort(probe::CompareSort s) { sort_ = s; }
    probe::CompareSort sort() const { return sort_; }

    // Most rows listed before the readout ends with "+N more".
    static constexpr size_t MAX_ROWS = 12;

    // Draw the readout for @p hovered (cursor inside its viewport) and its
    // X-linked peers in @p figure.  Returns true when anything was drawn.
    bool draw(const CursorReadout& cursor,
              Figure&              figure,
              Axes&                hovered,
              AxisLinkManager*     links,
              float                opacity,
              float                window_width,
              float                window_height,
              ImDrawList*          dl = nullptr);

   private:
    // Readout + markers on one axes at data x; anchor_x is the screen x of
    // the vertical line on that axes, anchor_y the preferred box centre.
    bool draw_for_axes(const Axes& axes,
                       double      data_x,
                       float       anchor_x,
                       float       anchor_y,
                       float       opacity,
                       float       window_width,
                       float       window_height,
                       ImDrawList* dl);

    ui::ThemeManager*  theme_mgr_ = nullptr;
    ImFont*            font_body_ = nullptr;
    bool               enabled_   = true;
    probe::ProbeMode   mode_      = probe::ProbeMode::Interpolate;
    probe::CompareSort sort_      = probe::CompareSort::ValueDescending;
};

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
