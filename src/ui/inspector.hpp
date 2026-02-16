#pragma once

#ifdef PLOTIX_USE_IMGUI

    #include <spectra/fwd.hpp>

    #include "selection_context.hpp"

struct ImFont;

namespace spectra::ui
{

class Inspector
{
   public:
    Inspector() = default;

    void set_context(const SelectionContext& ctx);
    const SelectionContext& context() const { return ctx_; }

    // Draw the inspector content within the given rect.
    // Call between ImGui::Begin/End of the inspector window.
    void draw(Figure& figure);

    // Set fonts (called once after font loading)
    void set_fonts(ImFont* body, ImFont* heading, ImFont* title);

   private:
    void draw_figure_properties(Figure& fig);
    void draw_axes_properties(Axes& ax, int index);
    void draw_series_properties(Series& s, int index);
    void draw_series_statistics(const Series& s);
    void draw_series_sparkline(const Series& s);

    // Draw axes-level statistics (aggregate across all series)
    void draw_axes_statistics(const Axes& ax);

    // Draw the series browser (list all series for quick selection)
    void draw_series_browser(Figure& fig);

    SelectionContext ctx_;

    // Collapsible section states
    bool sec_appearance_ = true;
    bool sec_margins_ = true;
    bool sec_legend_ = true;
    bool sec_data_ = true;
    bool sec_transform_ = false;
    bool sec_axis_x_ = true;
    bool sec_axis_y_ = true;
    bool sec_grid_ = true;
    bool sec_style_ = true;
    bool sec_stats_ = true;
    bool sec_quick_ = false;
    bool sec_preview_ = true;     // Sparkline preview
    bool sec_axes_stats_ = true;  // Per-axes aggregate stats

    // Fonts
    ImFont* font_body_ = nullptr;
    ImFont* font_heading_ = nullptr;
    ImFont* font_title_ = nullptr;
};

}  // namespace spectra::ui

#endif  // PLOTIX_USE_IMGUI
