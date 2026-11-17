#pragma once

// offscreen_export.hpp — export a copy of a figure with a headless App.
//
// The Export options dialog (file.export_dialog) needs what a window capture
// cannot give: any size / DPI, a transparent background, another theme or
// plot style, and a preview of all that.  Like the daemon's REQ_EXPORT_FIGURE
// (daemon/figure_export.cpp) it draws a copy of the figure with a headless App
// (offscreen Vulkan), through the same export path as Figure::save_png /
// save_svg / save_pdf in a headless program.  The copy is made with
// FigureSerializer (a .spectra file), so plugin series are not included.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ui/settings/export_settings.hpp"

namespace spectra
{

class Figure;

struct OffscreenExportJob
{
    std::string                figure_file;   ///< .spectra copy of the figure (snapshot_figure)
    std::string                out_path;      ///< file to write
    ui::settings::ExportFormat format = ui::settings::ExportFormat::Png;
    ui::settings::ResolvedExportSize size;
    std::string app_theme;      ///< theme the headless app starts with ("current")
    std::string app_palette;    ///< data palette it starts with
    std::string export_theme;   ///< ExportOptions::theme ("" = app_theme)
    std::string style;          ///< plot style to apply first ("" = keep)
    bool        transparent = false;
};

/// Write a .spectra copy of `figure` to `path` (UI thread).
bool snapshot_figure(const Figure& figure, const std::string& path);

/// A unique temporary file path with the given extension (no file is created).
std::string offscreen_temp_path(const std::string& extension);

/// Load the copy, apply the job's size / style and export it.  Blocks; safe to
/// run on a worker thread (it owns its own App, renderer and GPU device).
/// Returns false and sets `error` on failure.
bool run_offscreen_export(const OffscreenExportJob& job, std::string* error = nullptr);

struct PreviewImage
{
    std::vector<uint8_t> rgba;
    uint32_t             width  = 0;
    uint32_t             height = 0;
};

/// Render the job as a PNG (format and out_path are ignored) and return it
/// shrunk to fit max_w x max_h.  Empty image on failure.
PreviewImage render_offscreen_preview(OffscreenExportJob job, uint32_t max_w, uint32_t max_h);

}   // namespace spectra
