#pragma once

// export_options_dialog.hpp — the Export options dialog (command file.export_dialog).
//
// Format (PNG / SVG / PDF), theme, plot style, size in pixels or inches + DPI,
// PNG scale, transparent background, and a live preview of the result.  The
// choices are remembered in settings.json (SettingsData::export_dialog).  The
// file is written by a headless copy of the figure (ui/app/offscreen_export),
// so every option applies to all three formats the same way.

#ifdef SPECTRA_USE_IMGUI

    #include <cstdint>
    #include <functional>
    #include <future>
    #include <string>
    #include <vector>

    #include "render/backend.hpp"
    #include "ui/app/offscreen_export.hpp"
    #include "ui/settings/export_settings.hpp"

namespace spectra
{

class Figure;

namespace ui
{
class ThemeManager;
namespace settings
{
class SettingsStore;
}
}   // namespace ui

class ExportOptionsDialog
{
   public:
    ExportOptionsDialog() = default;
    ~ExportOptionsDialog();

    ExportOptionsDialog(const ExportOptionsDialog&)            = delete;
    ExportOptionsDialog& operator=(const ExportOptionsDialog&) = delete;

    void set_settings_store(ui::settings::SettingsStore* store) { store_ = store; }
    void set_theme_manager(ui::ThemeManager* tm) { theme_mgr_ = tm; }
    void set_last_export_dir(std::string* dir) { last_dir_ = dir; }

    /// Upload / release an RGBA preview image as an ImGui texture (the window's
    /// ImGuiIntegration).  Without them the preview is not shown.
    using CreateTextureFn  = std::function<bool(const PreviewImage&, TextureHandle*, uint64_t*)>;
    using DestroyTextureFn = std::function<void(TextureHandle, uint64_t)>;
    void set_texture_functions(CreateTextureFn create, DestroyTextureFn destroy)
    {
        create_texture_  = std::move(create);
        destroy_texture_ = std::move(destroy);
    }

    /// Open for the active figure (loads the remembered choices).
    void open();
    void close();
    bool is_open() const { return open_; }
    /// True while a preview is waiting to start or still rendering: the app
    /// must keep drawing frames so the dialog can start and collect it.
    bool wants_frames() const { return open_ && (preview_dirty_ || preview_job_.valid()); }

    /// Draw (once per ImGui frame) for `figure`, the window's active figure.
    void draw(Figure* figure);

    /// Current choices (tests / automation).
    const ui::settings::ExportDialogSettings& settings() const { return settings_; }
    void set_settings(const ui::settings::ExportDialogSettings& s);

    /// Build the job that exports `figure` with the current choices (the
    /// caller fills figure_file / out_path).
    OffscreenExportJob make_job(const Figure& figure) const;

    /// Export `figure` to `path` with the current choices, synchronously.
    bool export_to(const Figure& figure, const std::string& path, std::string* error = nullptr);

   private:
    void draw_options(const Figure& figure);
    void draw_preview(Figure& figure);
    void request_preview(Figure& figure);
    void poll_preview();
    void release_preview_texture();
    void remember_settings();
    void do_export(Figure& figure);

    ui::settings::SettingsStore* store_     = nullptr;
    ui::ThemeManager*            theme_mgr_ = nullptr;
    std::string*                 last_dir_  = nullptr;
    CreateTextureFn              create_texture_;
    DestroyTextureFn             destroy_texture_;

    ui::settings::ExportDialogSettings settings_;
    bool                               open_          = false;
    bool                               first_frame_   = false;
    bool                               preview_dirty_ = true;
    double                             dirty_since_   = 0.0;
    std::string                        status_;
    bool                               status_error_ = false;

    // Preview: one background render at a time; its texture is shown until
    // the next one is ready.  Released textures wait a few frames (in-flight
    // command buffers may still sample them).
    std::future<PreviewImage> preview_job_;
    std::string               preview_figure_file_;
    TextureHandle             preview_texture_;
    uint64_t                  preview_texture_id_ = 0;
    uint32_t                  preview_w_          = 0;
    uint32_t                  preview_h_          = 0;
    struct RetiredTexture
    {
        TextureHandle texture;
        uint64_t      id    = 0;
        int           frame = 0;
    };
    std::vector<RetiredTexture> retired_;
};

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
