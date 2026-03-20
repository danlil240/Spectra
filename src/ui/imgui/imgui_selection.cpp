#ifdef SPECTRA_USE_IMGUI

    #include "imgui_integration_internal.hpp"

namespace spectra
{

void ImGuiIntegration::select_series(Figure* fig, Axes* ax, int ax_idx, Series* s, int s_idx)
{
    bool shift_down = false;
    if (initialized_ && ImGui::GetCurrentContext() == imgui_context_)
        shift_down = ImGui::GetIO().KeyShift;

    // Shift+click: toggle in multi-selection (add/remove without clearing others)
    if (shift_down)
    {
        toggle_series_in_selection(fig, ax, static_cast<AxesBase*>(ax), ax_idx, s, s_idx);
        return;
    }

    // Toggle: re-clicking the same series deselects it
    if (selection_ctx_.type == ui::SelectionType::Series && selection_ctx_.series == s)
    {
        deselect_series();
        return;
    }

    selection_ctx_.select_series(fig, ax, ax_idx, s, s_idx);
    // Also set axes_base so 3D highlight/clipboard works
    selection_ctx_.axes_base = static_cast<AxesBase*>(ax);
    if (!selection_ctx_.selected_series.empty())
        selection_ctx_.selected_series[0].axes_base = static_cast<AxesBase*>(ax);
    // Switch inspector to Series section so it shows the right content when opened
    active_section_ = Section::Series;
    // Inspector is NOT auto-opened on series click — user opens it via nav rail or View menu
    SPECTRA_LOG_INFO("ui", "Series selected from canvas: " + s->label());
}

void ImGuiIntegration::select_series_no_toggle(Figure* fig,
                                               Axes*   ax,
                                               int     ax_idx,
                                               Series* s,
                                               int     s_idx)
{
    // If already selected as primary, do nothing (no toggle)
    if (selection_ctx_.type == ui::SelectionType::Series && selection_ctx_.is_selected(s))
        return;

    selection_ctx_.select_series(fig, ax, ax_idx, s, s_idx);
    selection_ctx_.axes_base = static_cast<AxesBase*>(ax);
    if (!selection_ctx_.selected_series.empty())
        selection_ctx_.selected_series[0].axes_base = static_cast<AxesBase*>(ax);
    active_section_ = Section::Series;
    // Inspector is NOT auto-opened on series click — user opens it via nav rail or View menu
    SPECTRA_LOG_INFO("ui", "Series selected (no-toggle): " + s->label());
}

void ImGuiIntegration::toggle_series_in_selection(Figure*   fig,
                                                  Axes*     ax,
                                                  AxesBase* ab,
                                                  int       ax_idx,
                                                  Series*   s,
                                                  int       s_idx)
{
    selection_ctx_.toggle_series(fig, ax, ab, ax_idx, s, s_idx);
    if (selection_ctx_.type == ui::SelectionType::Series)
    {
        active_section_ = Section::Series;
        // Inspector is NOT auto-opened on series click — user opens it via nav rail or View menu
    }
    SPECTRA_LOG_INFO("ui",
                     "Series toggled in multi-selection: " + s->label()
                         + " (count=" + std::to_string(selection_ctx_.selected_count()) + ")");
}

void ImGuiIntegration::deselect_series()
{
    if (selection_ctx_.type == ui::SelectionType::Series)
    {
        selection_ctx_.clear();
        SPECTRA_LOG_INFO("ui", "Series deselected");
    }
}

void ImGuiIntegration::select_series_in_rect(
    const std::vector<DataInteraction::RectSelectedEntry>& entries)
{
    if (entries.empty())
    {
        deselect_series();
        return;
    }

    // Clear previous selection and add all matched series
    selection_ctx_.clear();
    for (const auto& e : entries)
    {
        selection_ctx_.add_series(e.figure,
                                  e.axes,
                                  static_cast<AxesBase*>(e.axes),
                                  e.axes_index,
                                  e.series,
                                  e.series_index);
    }
    active_section_ = Section::Series;
    SPECTRA_LOG_INFO("ui", "Rectangle selected " + std::to_string(entries.size()) + " series");
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
