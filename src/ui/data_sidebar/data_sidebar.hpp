// data_sidebar.hpp — docked "Data" sidebar: every data source in one tree.
//
// Drawn in LayoutManager::left_panel_rect() (between the nav rail and the
// canvas) when the "core.data" shell panel is visible.  Lists the sources and
// fields of every registered DataProvider with a live search box; a field is
// plotted by double-click, Enter, the "Plot" context-menu item, or by dragging
// it onto a plot.  The floating panels (Log Browser, Topics) stay available
// through each provider's context menu.

#pragma once

#include <string>

#include "ui/data_sidebar/data_sidebar_model.hpp"

struct ImFont;

namespace spectra
{
class LayoutManager;
}

namespace spectra::ui::data_sidebar
{

class CsvProvider;

class DataSidebar
{
   public:
    DataSidebar()                              = default;
    ~DataSidebar()                             = default;
    DataSidebar(const DataSidebar&)            = delete;
    DataSidebar& operator=(const DataSidebar&) = delete;

    DataSidebarModel&       model() { return model_; }
    const DataSidebarModel& model() const { return model_; }

    // Layout zone (rect, width, visibility).  Not owned.
    void           set_layout_manager(LayoutManager* lm) { layout_ = lm; }
    LayoutManager* layout_manager() const { return layout_; }

    // CSV provider installed by install_builtin_providers() (may be null).
    void         set_csv_provider(CsvProvider* p) { csv_ = p; }
    CsvProvider* csv_provider() const { return csv_; }

    // Give the search box keyboard focus on the next draw (on open).
    void focus_search() { focus_search_ = true; }

    // Draw the sidebar in layout_manager()->left_panel_rect().  No-op when the
    // rect is empty or ImGui is not compiled in.
    void draw(ImFont* heading_font = nullptr, ImFont* icon_font = nullptr);

   private:
    void draw_rows(ImFont* icon_font);
    void run_pending_actions();

    DataSidebarModel model_;
    LayoutManager*   layout_ = nullptr;
    CsvProvider*     csv_    = nullptr;

    char   search_buf_[128]   = {};
    bool   focus_search_      = false;
    bool   scroll_to_sel_     = false;
    bool   was_visible_       = false;
    double last_refresh_time_ = -1.0;

    // Actions picked in this frame's rows / menus, run after drawing so the
    // provider lists are not changed while they are being drawn.
    enum class Pending
    {
        None,
        PlotRow,
        ToggleRow,
        StartSource,
        StopSource,
        CloseSource,
        OpenProvider,
        DetailsProvider,
    };
    Pending pending_     = Pending::None;
    size_t  pending_row_ = 0;
};

}   // namespace spectra::ui::data_sidebar
