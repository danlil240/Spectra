#include "offscreen_export.hpp"

#include <spectra/app.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <system_error>

#include "stb_image.h"
#include "ui/app/window_png_export.hpp"
#include "ui/workspace/figure_serializer.hpp"

#ifdef _WIN32
    #include <process.h>
    #define SPECTRA_GETPID _getpid
#else
    #include <unistd.h>
    #define SPECTRA_GETPID getpid
#endif

namespace spectra
{

namespace fs = std::filesystem;

bool snapshot_figure(const Figure& figure, const std::string& path)
{
    return FigureSerializer::save(path, figure);
}

std::string offscreen_temp_path(const std::string& extension)
{
    static std::atomic<uint64_t> counter{0};
    std::error_code              ec;
    fs::path                     dir = fs::temp_directory_path(ec);
    if (ec)
        dir = ".";
    const auto stamp =
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string name = "spectra-export-" + std::to_string(SPECTRA_GETPID()) + "-"
                             + std::to_string(stamp) + "-" + std::to_string(counter.fetch_add(1))
                             + "." + extension;
    return (dir / name).string();
}

bool run_offscreen_export(const OffscreenExportJob& job, std::string* error)
{
    auto fail = [&](const std::string& msg)
    {
        if (error)
            *error = msg;
        SPECTRA_LOG_ERROR("export", "{}", msg);
        return false;
    };

    // Headless: renders offscreen even when SPECTRA_SOCKET is set.  The user's
    // settings are not read: the job carries the theme and palette.
    AppConfig cfg;
    cfg.headless     = true;
    cfg.theme        = job.app_theme;
    cfg.data_palette = job.app_palette;
    App app(cfg);
    if (!app.ok())
        return fail("Export: no render backend available (Vulkan failed to initialize)");

    auto fig = std::make_unique<Figure>();
    if (!FigureSerializer::load(job.figure_file, *fig))
        return fail("Export: could not copy the figure");
    if (!job.style.empty() && !fig->apply_style(job.style))
        return fail("Export: unknown plot style '" + job.style + "'");
    fig->set_size(job.size.layout_width, job.size.layout_height);
    // A file is one frame: never let a live figure keep the runtime spinning.
    fig->anim_ = FigureAnimState{};

    ExportOptions opts;
    opts.theme       = job.export_theme;
    opts.transparent = job.transparent;
    bool queued      = false;
    switch (job.format)
    {
        case ui::settings::ExportFormat::Svg:
            queued = fig->save_svg(job.out_path, opts);
            break;
        case ui::settings::ExportFormat::Pdf:
            queued = fig->save_pdf(job.out_path, opts);
            break;
        case ui::settings::ExportFormat::Png:
        default:
            opts.width  = job.size.pixel_width;
            opts.height = job.size.pixel_height;
            queued      = fig->save_png(job.out_path, opts);
            break;
    }
    if (!queued)
        return fail("Export: cannot write to '" + job.out_path + "'");
    app.figure_registry().register_figure(std::move(fig));
    app.run();

    std::error_code ec;
    if (!fs::exists(job.out_path, ec) || fs::file_size(job.out_path, ec) == 0 || ec)
        return fail("Export: rendering or writing '" + job.out_path + "' failed");
    return true;
}

PreviewImage render_offscreen_preview(OffscreenExportJob job, uint32_t max_w, uint32_t max_h)
{
    PreviewImage out;
    if (max_w == 0 || max_h == 0)
        return out;
    // The preview is the PNG at its layout size (pixel scale 1): the same
    // layout, text and line proportions as the file, at a fraction of the cost.
    job.format            = ui::settings::ExportFormat::Png;
    job.out_path          = offscreen_temp_path("png");
    job.size.pixel_width  = job.size.layout_width;
    job.size.pixel_height = job.size.layout_height;
    job.size.pixel_scale  = 1.0f;
    if (run_offscreen_export(job))
    {
        int      w = 0, h = 0, n = 0;
        uint8_t* px = stbi_load(job.out_path.c_str(), &w, &h, &n, 4);
        if (px && w > 0 && h > 0)
        {
            const double s =
                std::min(1.0,
                         std::min(static_cast<double>(max_w) / w, static_cast<double>(max_h) / h));
            out.width  = std::max(1u, static_cast<uint32_t>(w * s));
            out.height = std::max(1u, static_cast<uint32_t>(h * s));
            out.rgba.resize(static_cast<size_t>(out.width) * out.height * 4);
            fit_rgba_image(px,
                           static_cast<uint32_t>(w),
                           static_cast<uint32_t>(h),
                           out.rgba.data(),
                           out.width,
                           out.height);
        }
        if (px)
            stbi_image_free(px);
    }
    std::error_code ec;
    fs::remove(job.out_path, ec);
    return out;
}

}   // namespace spectra
