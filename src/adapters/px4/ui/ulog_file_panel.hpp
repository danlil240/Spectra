#pragma once

// ULogFilePanel — ImGui panel for loading and browsing ULog files.
//
// Displays file metadata, message formats, info keys, parameters, and
// log messages from a loaded ULog file.  Supports drag-and-drop of fields
// to the plot area.

#include "../px4_plot_manager.hpp"
#include "../ulog_reader.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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

    // Draw the panel.  Call once per frame inside an ImGui context.
    void draw(bool* p_open = nullptr);

    // Set callback for when a field is double-clicked (add to plot).
    using FieldCallback = std::function<void(const std::string& topic,
                                             const std::string& field,
                                             int array_idx)>;
    void set_field_callback(FieldCallback cb) { field_cb_ = std::move(cb); }

    // Set callback for file open requests.
    using FileCallback = std::function<void(const std::string& path)>;
    void set_file_callback(FileCallback cb) { file_cb_ = std::move(cb); }

    // Panel title.
    const std::string& title() const { return title_; }
    void set_title(const std::string& t) { title_ = t; }

    // Last opened file path.
    const std::string& current_file() const { return current_file_; }

private:
    void draw_file_header();
    void draw_metadata();
    void draw_topic_tree();
    void draw_info_table();
    void draw_params_table();
    void draw_log_messages();

    ULogReader&     reader_;
    Px4PlotManager& plot_mgr_;

    std::string title_{"ULog File"};
    std::string current_file_;

    // Topic tree filter.
    char topic_filter_buf_[128]{};

    FieldCallback field_cb_;
    FileCallback  file_cb_;
};

}   // namespace spectra::adapters::px4
