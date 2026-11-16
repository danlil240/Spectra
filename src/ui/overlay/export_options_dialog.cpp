#ifdef SPECTRA_USE_IMGUI

    #include "export_options_dialog.hpp"

    #include <imgui.h>
    #include <spectra/figure.hpp>
    #include <spectra/logger.hpp>
    #include <spectra/style.hpp>

    #include <algorithm>
    #include <chrono>
    #include <cmath>
    #include <filesystem>
    #include <format>
    #include <system_error>

    #include "ui/export_dialog.hpp"
    #include "ui/imgui/widgets.hpp"
    #include "ui/settings/settings_store.hpp"
    #include "ui/theme/theme.hpp"
    #include "ui/theme/theme_imgui.hpp"

namespace spectra
{

namespace
{

using ui::settings::ExportDialogSettings;
using ui::settings::ExportFormat;
using ui::settings::ExportSizeUnit;

constexpr float  OPTIONS_WIDTH     = 300.0f;
constexpr float  PREVIEW_WIDTH     = 360.0f;
constexpr float  PREVIEW_HEIGHT    = 260.0f;
constexpr double PREVIEW_DEBOUNCE  = 0.3;   // seconds after the last change
constexpr int    TEXTURE_KEEPALIVE = 4;     // frames a released preview stays alive

const char* const FORMAT_LABELS[] = {"PNG", "SVG", "PDF"};
const char* const THEME_LABELS[]  = {"Current", "Light", "Dark"};
const char* const THEME_IDS[]     = {"current", "light", "dark"};
const char* const UNIT_LABELS[]   = {"Pixels", "Inches"};
const float       SCALES[]        = {1.0f, 2.0f, 3.0f, 4.0f};
const char* const SCALE_LABELS[]  = {"1x", "2x", "3x", "4x"};

int index_of_theme(const std::string& theme)
{
    for (int i = 0; i < 3; ++i)
        if (theme == THEME_IDS[i])
            return i;
    return 0;
}

}   // namespace

ExportOptionsDialog::~ExportOptionsDialog()
{
    // The worker owns its own App; wait so it never outlives the dialog.
    if (preview_job_.valid())
        preview_job_.wait();
    std::error_code ec;
    if (!preview_figure_file_.empty())
        std::filesystem::remove(preview_figure_file_, ec);
}

void ExportOptionsDialog::open()
{
    settings_ =
        store_ ? ui::settings::sanitized(store_->data().export_dialog) : ExportDialogSettings{};
    open_          = true;
    first_frame_   = true;
    preview_dirty_ = true;
    dirty_since_   = -1.0;   // render the first preview right away
    status_.clear();
    status_error_ = false;
}

void ExportOptionsDialog::close()
{
    if (open_)
        remember_settings();
    open_ = false;
    release_preview_texture();
}

void ExportOptionsDialog::set_settings(const ExportDialogSettings& s)
{
    settings_      = ui::settings::sanitized(s);
    preview_dirty_ = true;
}

void ExportOptionsDialog::remember_settings()
{
    if (!store_)
        return;
    if (store_->data().export_dialog == settings_)
        return;
    store_->data_mut().export_dialog = settings_;
    store_->notify_change();
}

OffscreenExportJob ExportOptionsDialog::make_job(const Figure& figure) const
{
    OffscreenExportJob job;
    job.format = settings_.format;
    job.size   = ui::settings::resolve_export_size(settings_, figure.width(), figure.height());
    if (theme_mgr_)
    {
        job.app_theme   = theme_mgr_->current_theme_name();
        job.app_palette = theme_mgr_->current_data_palette_name();
    }
    job.export_theme = ui::settings::export_theme_name(settings_);
    job.style        = settings_.style;
    job.transparent  = settings_.transparent;
    return job;
}

bool ExportOptionsDialog::export_to(const Figure&      figure,
                                    const std::string& path,
                                    std::string*       error)
{
    OffscreenExportJob job = make_job(figure);
    job.out_path           = path;
    job.figure_file        = offscreen_temp_path("spectra");
    bool ok                = snapshot_figure(figure, job.figure_file);
    if (!ok && error)
        *error = "Export: could not copy the figure";
    if (ok)
        ok = run_offscreen_export(job, error);
    std::error_code ec;
    std::filesystem::remove(job.figure_file, ec);
    return ok;
}

void ExportOptionsDialog::release_preview_texture()
{
    if (preview_texture_id_ != 0 || preview_texture_)
        retired_.push_back({preview_texture_, preview_texture_id_, ImGui::GetFrameCount()});
    preview_texture_    = {};
    preview_texture_id_ = 0;
    preview_w_          = 0;
    preview_h_          = 0;
}

void ExportOptionsDialog::request_preview(Figure& figure)
{
    if (preview_job_.valid())
        return;   // one at a time; the dirty flag stays set until it can start
    if (!create_texture_)
    {
        preview_dirty_ = false;
        return;
    }
    OffscreenExportJob job = make_job(figure);
    job.figure_file        = offscreen_temp_path("spectra");
    if (!snapshot_figure(figure, job.figure_file))
    {
        preview_dirty_ = false;
        return;
    }
    preview_figure_file_ = job.figure_file;
    preview_dirty_       = false;
    // Twice the shown size so it stays crisp on HiDPI screens.
    const auto max_w = static_cast<uint32_t>(PREVIEW_WIDTH * 2.0f);
    const auto max_h = static_cast<uint32_t>(PREVIEW_HEIGHT * 2.0f);
    preview_job_     = std::async(std::launch::async,
                              [job, max_w, max_h]()
                              {
                                  PreviewImage    img = render_offscreen_preview(job, max_w, max_h);
                                  std::error_code ec;
                                  std::filesystem::remove(job.figure_file, ec);
                                  return img;
                              });
}

void ExportOptionsDialog::poll_preview()
{
    if (!preview_job_.valid()
        || preview_job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    PreviewImage img = preview_job_.get();
    preview_figure_file_.clear();
    if (!open_ || img.rgba.empty() || !create_texture_)
        return;
    TextureHandle tex;
    uint64_t      id = 0;
    if (!create_texture_(img, &tex, &id))
        return;
    release_preview_texture();
    preview_texture_    = tex;
    preview_texture_id_ = id;
    preview_w_          = img.width;
    preview_h_          = img.height;
}

void ExportOptionsDialog::do_export(Figure& figure)
{
    const ExportFormat fmt            = settings_.format;
    const char*        ext            = ui::settings::export_format_extension(fmt);
    const std::string  pattern        = std::string("*.") + ext;
    const char*        patterns[]     = {pattern.c_str()};
    const std::string  default_name   = std::string("spectra_export.") + ext;
    const char* const  descriptions[] = {"PNG Image", "SVG Image", "PDF Document"};
    std::string        scratch_dir;
    auto               path = ask_export_path("Export Figure",
                                default_name.c_str(),
                                1,
                                patterns,
                                descriptions[static_cast<int>(fmt)],
                                last_dir_ ? *last_dir_ : scratch_dir);
    if (!path)
        return;
    std::string target = *path;
    if (std::filesystem::path(target).extension().empty())
        target = ui::settings::with_export_extension(target, fmt);

    remember_settings();
    std::string error;
    if (export_to(figure, target, &error))
    {
        SPECTRA_LOG_INFO("export", "Exported {}", target);
        status_       = "Saved " + target;
        status_error_ = false;
        close();
    }
    else
    {
        status_       = error.empty() ? "Export failed" : error;
        status_error_ = true;
    }
}

void ExportOptionsDialog::draw(Figure* figure)
{
    // Free released preview textures once no frame in flight can use them.
    if (!retired_.empty())
    {
        const int now = ImGui::GetFrameCount();
        auto      it  = retired_.begin();
        while (it != retired_.end())
        {
            if (now - it->frame >= TEXTURE_KEEPALIVE)
            {
                if (destroy_texture_)
                    destroy_texture_(it->texture, it->id);
                it = retired_.erase(it);
            }
            else
                ++it;
        }
    }
    poll_preview();
    if (!open_)
        return;
    if (!figure)
    {
        close();
        return;
    }

    const auto& c = ui::theme();
    if (first_frame_)
    {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
                                ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowFocus();
        first_frame_ = false;
    }
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::to_imvec4(c.bg_elevated));
    ImGui::PushStyleColor(ImGuiCol_Border, ui::to_imvec4(c.border_default));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
    bool keep_open = true;
    if (ImGui::Begin("Export Figure###spectra_export_options",
                     &keep_open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize
                         | ImGuiWindowFlags_NoSavedSettings))
    {
        const ExportDialogSettings before = settings_;

        ImGui::BeginGroup();
        // Fixed width, height follows the rows (pixel and inch modes differ).
        ImGui::BeginChild("##export_opts",
                          ImVec2(OPTIONS_WIDTH, 0.0f),
                          ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        draw_options(*figure);
        ImGui::EndChild();
        ImGui::EndGroup();

        ImGui::SameLine(0.0f, 16.0f);
        draw_preview(*figure);

        if (!(settings_ == before))
        {
            preview_dirty_ = true;
            dirty_since_   = ImGui::GetTime();
        }

        ImGui::Spacing();
        if (!status_.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ui::to_imvec4(status_error_ ? c.error : c.text_secondary));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + OPTIONS_WIDTH + PREVIEW_WIDTH);
            ImGui::TextUnformatted(status_.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }

        // Buttons, right-aligned.
        const float button_w = 96.0f;
        const float avail    = OPTIONS_WIDTH + 16.0f + PREVIEW_WIDTH;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - 2.0f * button_w - 8.0f);
        if (ImGui::Button("Cancel", ImVec2(button_w, 0.0f)))
            keep_open = false;
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ui::to_imvec4(c.accent));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::to_imvec4(c.accent_hover));
        ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(c.text_inverse));
        const bool do_it = ImGui::Button("Export...", ImVec2(button_w, 0.0f));
        ImGui::PopStyleColor(3);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && ImGui::IsWindowFocused())
            keep_open = false;
        if (do_it)
            do_export(*figure);
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    if (!keep_open)
    {
        close();
        return;
    }
    if (open_ && preview_dirty_ && ImGui::GetTime() - dirty_since_ >= PREVIEW_DEBOUNCE)
        request_preview(*figure);
}

void ExportOptionsDialog::draw_options(const Figure& figure)
{
    ExportDialogSettings& s = settings_;

    int fmt = static_cast<int>(s.format);
    if (ui::widgets::combo_field("Format", fmt, FORMAT_LABELS, 3))
        s.format = static_cast<ExportFormat>(std::clamp(fmt, 0, 2));

    int theme_idx = index_of_theme(s.theme);
    if (ui::widgets::combo_field("Theme", theme_idx, THEME_LABELS, 3))
        s.theme = THEME_IDS[std::clamp(theme_idx, 0, 2)];

    // Plot style: the figure's own, or a built-in preset for this file only.
    {
        std::vector<std::string> names = style::available();
        std::vector<std::string> labels;
        labels.push_back("Figure's own");
        for (const auto& n : names)
            labels.push_back(style::display_name(n));
        std::vector<const char*> items;
        for (const auto& l : labels)
            items.push_back(l.c_str());
        int cur = 0;
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] == s.style)
                cur = static_cast<int>(i) + 1;
        if (ui::widgets::combo_field("Style", cur, items.data(), static_cast<int>(items.size())))
            s.style = cur <= 0 ? std::string() : names[static_cast<size_t>(cur - 1)];
    }

    ui::widgets::small_spacing();
    int unit = static_cast<int>(s.unit);
    if (ui::widgets::combo_field("Size in", unit, UNIT_LABELS, 2))
        s.unit = unit == 1 ? ExportSizeUnit::Inches : ExportSizeUnit::Pixels;

    if (s.unit == ExportSizeUnit::Pixels)
    {
        int w = s.width_px > 0 ? static_cast<int>(s.width_px) : static_cast<int>(figure.width());
        int h = s.height_px > 0 ? static_cast<int>(s.height_px) : static_cast<int>(figure.height());
        if (ui::widgets::int_drag_field("Width", w, 4, 16, 16384, "%d px"))
            s.width_px = static_cast<uint32_t>(std::max(16, w));
        if (ui::widgets::int_drag_field("Height", h, 4, 16, 16384, "%d px"))
            s.height_px = static_cast<uint32_t>(std::max(16, h));
        if (s.width_px > 0 || s.height_px > 0)
        {
            if (ImGui::SmallButton("Use window size"))
            {
                s.width_px  = 0;
                s.height_px = 0;
            }
        }
    }
    else
    {
        ui::widgets::drag_field("Width", s.width_in, 0.05f, 0.5f, 100.0f, "%.2f in");
        ui::widgets::drag_field("Height", s.height_in, 0.05f, 0.5f, 100.0f, "%.2f in");
        ui::widgets::drag_field("DPI", s.dpi, 1.0f, 36.0f, 1200.0f, "%.0f");
    }

    const bool raster = s.format == ExportFormat::Png;
    ImGui::BeginDisabled(!raster);
    int scale_idx = 0;
    for (int i = 0; i < 4; ++i)
        if (std::abs(s.scale - SCALES[i]) < 1e-3f)
            scale_idx = i;
    if (ui::widgets::combo_field("Scale", scale_idx, SCALE_LABELS, 4))
        s.scale = SCALES[std::clamp(scale_idx, 0, 3)];
    ImGui::EndDisabled();

    ui::widgets::checkbox_field("Transparent background", s.transparent);

    s = ui::settings::sanitized(s);

    // What the file will be.
    ui::widgets::small_spacing();
    const auto  r = ui::settings::resolve_export_size(s, figure.width(), figure.height());
    std::string summary;
    if (raster)
        summary = std::format("{} x {} px", r.pixel_width, r.pixel_height);
    else
        summary = std::format("{:.2f} x {:.2f} in page ({} x {} px)",
                              r.layout_width / ui::settings::LAYOUT_PX_PER_INCH,
                              r.layout_height / ui::settings::LAYOUT_PX_PER_INCH,
                              r.layout_width,
                              r.layout_height);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(ui::theme().text_secondary));
    ImGui::TextUnformatted(summary.c_str());
    ImGui::PopStyleColor();
}

void ExportOptionsDialog::draw_preview(Figure& figure)
{
    const auto& c = ui::theme();
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(c.text_secondary));
    ImGui::TextUnformatted("Preview");
    ImGui::PopStyleColor();

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 size(PREVIEW_WIDTH, PREVIEW_HEIGHT);
    ImGui::InvisibleButton("##export_preview", size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), ui::to_imu32(c.bg_secondary), 4.0f);

    if (preview_texture_id_ != 0 && preview_w_ > 0 && preview_h_ > 0)
    {
        const float  s = std::min(size.x / preview_w_, size.y / preview_h_);
        const ImVec2 img(preview_w_ * s, preview_h_ * s);
        const ImVec2 a(p0.x + (size.x - img.x) * 0.5f, p0.y + (size.y - img.y) * 0.5f);
        const ImVec2 b(a.x + img.x, a.y + img.y);
        if (settings_.transparent)
        {
            // Checkerboard behind a transparent export.
            constexpr float cell = 8.0f;
            dl->PushClipRect(a, b, true);
            for (float y = a.y; y < b.y; y += cell)
                for (float x = a.x; x < b.x; x += cell)
                {
                    const bool odd =
                        (static_cast<int>((x - a.x) / cell) + static_cast<int>((y - a.y) / cell))
                        % 2;
                    dl->AddRectFilled(ImVec2(x, y),
                                      ImVec2(x + cell, y + cell),
                                      ui::to_imu32(odd ? c.bg_tertiary : c.bg_primary));
                }
            dl->PopClipRect();
        }
        dl->AddImage(static_cast<ImTextureID>(preview_texture_id_), a, b);
        dl->AddRect(a, b, ui::to_imu32(c.border_subtle));
    }
    if (preview_job_.valid() || (preview_dirty_ && preview_texture_id_ == 0))
    {
        const char*  msg = "Rendering preview...";
        const ImVec2 ts  = ImGui::CalcTextSize(msg);
        const ImVec2 at(p0.x + (size.x - ts.x) * 0.5f, p0.y + size.y - ts.y - 8.0f);
        dl->AddRectFilled(ImVec2(at.x - 6.0f, at.y - 3.0f),
                          ImVec2(at.x + ts.x + 6.0f, at.y + ts.y + 3.0f),
                          ui::to_imu32(c.bg_elevated, 0.9f),
                          4.0f);
        dl->AddText(at, ui::to_imu32(c.text_secondary), msg);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Click to refresh the preview");
    if (ImGui::IsItemClicked())
    {
        preview_dirty_ = true;
        dirty_since_   = -1.0;
    }
    (void)figure;
    ImGui::EndGroup();
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
