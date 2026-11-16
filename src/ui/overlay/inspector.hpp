#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <cstdint>
    #include <functional>
    #include <spectra/axes.hpp>
    #include <spectra/fwd.hpp>
    #include <string>

    #include "ui/input/selection_context.hpp"
    #include "ui/viewmodel/figure_view_model.hpp"

struct ImFont;

namespace spectra
{
class CommandRegistry;
class PluginManager;
}   // namespace spectra

namespace spectra::ui
{

class Inspector
{
   public:
    Inspector() = default;

    void                    set_context(const SelectionContext& ctx);
    const SelectionContext& context() const { return ctx_; }

    // Set the active FigureViewModel (for sub-ViewModel access)
    void             set_figure_view_model(FigureViewModel* vm) { figure_vm_ = vm; }
    FigureViewModel* figure_view_model() const { return figure_vm_; }

    // Draw the inspector content within the given rect.
    // Call between ImGui::Begin/End of the inspector window.
    void draw(Figure& figure);

    // Set fonts (called once after font loading)
    void set_fonts(ImFont* body, ImFont* heading, ImFont* title);

    // Series clipboard (not owned)
    void             set_series_clipboard(SeriesClipboard* sc) { clipboard_ = sc; }
    SeriesClipboard* series_clipboard() const { return clipboard_; }

    // Deferred series removal callback (set by ImGuiIntegration)
    using DeferRemovalFn = std::function<void(AxesBase*, Series*)>;
    void set_defer_series_removal(DeferRemovalFn fn) { defer_removal_ = std::move(fn); }

    // Command registry (not owned): the Style picker runs style.<name> so the
    // change is undoable like the palette command.
    void set_command_registry(CommandRegistry* cr) { command_registry_ = cr; }

    // Axis range edited in the X / Y AXIS "Range" fields: @p ax already has
    // its new limits, @p old_x / @p old_y are the previous ones.  Calls of one
    // continuous edit (a drag) share @p edit_key.  ImGuiIntegration records
    // it in the view history and moves linked axes along.
    using AxesLimitsEditFn = std::function<
        void(Axes& ax, AxisLimits old_x, AxisLimits old_y, const std::string& edit_key)>;
    void set_on_axes_limits_edited(AxesLimitsEditFn fn) { on_limits_edited_ = std::move(fn); }

    /// Draw plugin status section (call from app context when plugin_mgr is available).
    void draw_plugin_status(const PluginManager& plugin_mgr);

   private:
    void draw_figure_properties(Figure& fig);
    void draw_style_picker(Figure& fig);
    void draw_axes_properties(Axes& ax, int index);
    void draw_axes3d_properties(Axes3D& ax, int index);

    // Axes picker shown above the Axes properties when the figure has several
    // axes (2D and 3D); changes ctx_ to the picked axes.
    void draw_axes_picker(Figure& fig);
    void draw_series_properties(Series& s, int index);
    void draw_series_statistics(const Series& s);
    void draw_series_sparkline(const Series& s);

    // Draw axes-level statistics (aggregate across all series)
    void draw_axes_statistics(const Axes& ax);

    // Draw reference lines management (list + delete)
    void draw_reference_lines(Axes& ax);

    // Draw the series browser (list all series for quick selection)
    void draw_series_browser(Figure& fig);

    SelectionContext ctx_;
    FigureViewModel* figure_vm_        = nullptr;
    SeriesClipboard* clipboard_        = nullptr;
    CommandRegistry* command_registry_ = nullptr;
    DeferRemovalFn   defer_removal_;
    AxesLimitsEditFn on_limits_edited_;

    // Axis range edit in progress (one key per drag / typed edit).
    bool     xlim_edit_active_   = false;
    bool     ylim_edit_active_   = false;
    uint64_t limits_edit_serial_ = 0;

    // Collapsible section states
    bool sec_appearance_ = true;
    bool sec_plot_style_ = true;
    bool sec_margins_    = true;
    bool sec_legend_     = true;
    bool sec_axis_x_     = true;
    bool sec_axis_y_     = true;
    bool sec_axis_z_     = true;
    bool sec_lighting_   = true;
    bool sec_camera_     = true;
    bool sec_grid_       = true;
    bool sec_style_      = true;
    bool sec_stats_      = true;
    bool sec_quick_      = false;
    bool sec_preview_    = true;   // Sparkline preview
    bool sec_axes_stats_ = true;   // Per-axes aggregate stats
    bool sec_ref_lines_  = true;   // Reference lines management

    // Drag-reorder state (deferred to avoid mid-iteration mutation)
    struct PendingMove
    {
        AxesBase* axes = nullptr;
        int       from = -1;
        int       to   = -1;
    };
    PendingMove pending_move_;

    // Fonts
    ImFont* font_body_    = nullptr;
    ImFont* font_heading_ = nullptr;
    ImFont* font_title_   = nullptr;
};

}   // namespace spectra::ui

#endif   // SPECTRA_USE_IMGUI
