#pragma once

// ULogFilePanel — ImGui panel for loading and browsing ULog files.
//
// Displays file metadata, message formats, info keys, parameters, and
// log messages from a loaded ULog file.  Supports drag-and-drop of fields
// to the plot area.

#include "../px4_plot_manager.hpp"
#include "../ulog_reader.hpp"

#include <ui/panel/panel_detach_controller.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace spectra
{
class WindowManager;
}

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// ULogFilePanel
// ---------------------------------------------------------------------------

class ULogFilePanel
{
   public:
    explicit ULogFilePanel(ULogReader& reader, Px4PlotManager& plot_mgr);
    ~ULogFilePanel();

    ULogFilePanel(const ULogFilePanel&)            = delete;
    ULogFilePanel& operator=(const ULogFilePanel&) = delete;

    // Set callback for when a field is double-clicked (add to plot).
    using FieldCallback =
        std::function<void(const std::string& topic, const std::string& field, int array_idx)>;
    void set_field_callback(FieldCallback cb) { field_cb_ = std::move(cb); }

    // Set callback for file open requests.
    using FileCallback = std::function<void(const std::string& path)>;
    void set_file_callback(FileCallback cb) { file_cb_ = std::move(cb); }

    // Last opened file path.
    const std::string& current_file() const { return current_file_; }

    /// Draw the panel — handles ImGui Begin/End and controller state.
    void draw(bool* p_open = nullptr);

    /// Process deferred OS window create/destroy.
    void process_pending() { detach_ctrl_.process_pending(); }

    /// Wire WindowManager for OS-level panel tearoff.
    void set_window_manager(spectra::WindowManager* wm) { detach_ctrl_.set_window_manager(wm); }

    /// Access the detach controller.
    spectra::PanelDetachController&       detach_controller() { return detach_ctrl_; }
    const spectra::PanelDetachController& detach_controller() const { return detach_ctrl_; }

    // Convenience forwarders.
    const std::string& title() const { return detach_ctrl_.title(); }
    void               set_dock_id(uint32_t id) { detach_ctrl_.set_dock_id(id); }
    bool               is_detached() const { return detach_ctrl_.is_detached(); }
    void               detach() { detach_ctrl_.detach(); }
    void               attach() { detach_ctrl_.attach(); }

   private:
    void draw_content();
    void draw_context_menu();
    void draw_file_header();
    void draw_metadata();
    void draw_topic_tree();
    void draw_info_table();
    void draw_params_table();
    void draw_log_messages();

    spectra::PanelDetachController detach_ctrl_;

    // Track docked state for undock-transition detection.
    bool was_docked_{true};

    ULogReader&     reader_;
    Px4PlotManager& plot_mgr_;

    std::string current_file_;

    // Topic tree filter.
    char topic_filter_buf_[128]{};

    FieldCallback field_cb_;
    FileCallback  file_cb_;
};

}   // namespace spectra::adapters::px4
