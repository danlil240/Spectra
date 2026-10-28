#include "window_png_export.hpp"

#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

#include "platform/clipboard_image.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "render/vulkan/window_context.hpp"
#include "ui/theme/svg_style.hpp"
#include "ui/theme/theme.hpp"

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    #include "ui/window/window_manager.hpp"
#endif

namespace spectra
{

namespace
{

// Resample one channel-interleaved RGBA8 image into a cw x ch block of dst
// (row stride dst_w) starting at (ox, oy).
void resample_rgba(const uint8_t* src,
                   uint32_t       src_w,
                   uint32_t       src_h,
                   uint8_t*       dst,
                   uint32_t       dst_w,
                   uint32_t       ox,
                   uint32_t       oy,
                   uint32_t       cw,
                   uint32_t       ch)
{
    const double sx         = static_cast<double>(src_w) / static_cast<double>(cw);
    const double sy         = static_cast<double>(src_h) / static_cast<double>(ch);
    const size_t src_stride = static_cast<size_t>(src_w) * 4;

    for (uint32_t y = 0; y < ch; ++y)
    {
        uint8_t* drow = dst + (static_cast<size_t>(oy) + y) * dst_w * 4 + ox * 4;
        for (uint32_t x = 0; x < cw; ++x)
        {
            double acc[4] = {0.0, 0.0, 0.0, 0.0};
            if (sx > 1.0 || sy > 1.0)
            {
                // Shrinking (on at least one axis): average the covered source area.
                const double x0   = x * sx;
                const double x1   = std::min<double>(src_w, x0 + std::max(sx, 1.0));
                const double y0   = y * sy;
                const double y1   = std::min<double>(src_h, y0 + std::max(sy, 1.0));
                double       wsum = 0.0;
                for (auto yi = static_cast<uint32_t>(y0); yi < src_h && yi < y1; ++yi)
                {
                    const double wy = std::min<double>(yi + 1, y1) - std::max<double>(yi, y0);
                    if (wy <= 0.0)
                        continue;
                    const uint8_t* srow = src + yi * src_stride;
                    for (auto xi = static_cast<uint32_t>(x0); xi < src_w && xi < x1; ++xi)
                    {
                        const double wx = std::min<double>(xi + 1, x1) - std::max<double>(xi, x0);
                        if (wx <= 0.0)
                            continue;
                        const double w = wx * wy;
                        for (int c = 0; c < 4; ++c)
                            acc[c] += w * srow[xi * 4 + c];
                        wsum += w;
                    }
                }
                if (wsum > 0.0)
                    for (double& a : acc)
                        a /= wsum;
            }
            else
            {
                // Enlarging: bilinear between the nearest source pixel centers.
                const double fx  = std::clamp((x + 0.5) * sx - 0.5, 0.0, src_w - 1.0);
                const double fy  = std::clamp((y + 0.5) * sy - 0.5, 0.0, src_h - 1.0);
                const auto   x0  = static_cast<uint32_t>(fx);
                const auto   y0  = static_cast<uint32_t>(fy);
                const auto   x1  = std::min(x0 + 1, src_w - 1);
                const auto   y1  = std::min(y0 + 1, src_h - 1);
                const double tx  = fx - x0;
                const double ty  = fy - y0;
                const auto*  p00 = src + y0 * src_stride + x0 * 4;
                const auto*  p10 = src + y0 * src_stride + x1 * 4;
                const auto*  p01 = src + y1 * src_stride + x0 * 4;
                const auto*  p11 = src + y1 * src_stride + x1 * 4;
                for (int c = 0; c < 4; ++c)
                {
                    const double top = p00[c] + (p10[c] - p00[c]) * tx;
                    const double bot = p01[c] + (p11[c] - p01[c]) * tx;
                    acc[c]           = top + (bot - top) * ty;
                }
            }
            for (int c = 0; c < 4; ++c)
                drow[x * 4 + c] = static_cast<uint8_t>(std::clamp(std::lround(acc[c]), 0L, 255L));
        }
    }
}

}   // namespace

void fit_rgba_image(const uint8_t* src,
                    uint32_t       src_w,
                    uint32_t       src_h,
                    uint8_t*       dst,
                    uint32_t       dst_w,
                    uint32_t       dst_h)
{
    if (!dst || dst_w == 0 || dst_h == 0)
        return;
    std::fill(dst, dst + static_cast<size_t>(dst_w) * dst_h * 4, uint8_t{0});
    if (!src || src_w == 0 || src_h == 0)
        return;

    if (src_w == dst_w && src_h == dst_h)
    {
        std::copy(src, src + static_cast<size_t>(src_w) * src_h * 4, dst);
        return;
    }

    const double scale =
        std::min(static_cast<double>(dst_w) / src_w, static_cast<double>(dst_h) / src_h);
    const auto cw = std::clamp(static_cast<uint32_t>(std::lround(src_w * scale)), 1u, dst_w);
    const auto ch = std::clamp(static_cast<uint32_t>(std::lround(src_h * scale)), 1u, dst_h);
    resample_rgba(src, src_w, src_h, dst, dst_w, (dst_w - cw) / 2, (dst_h - ch) / 2, cw, ch);
}

void WindowPngExport::reset()
{
    path_.clear();
    theme_.clear();
    requested_width_      = 0;
    requested_height_     = 0;
    active_               = false;
    copy_to_clipboard_    = false;
    awaiting_gpu_capture_ = false;
}

void WindowPngExport::collect(VulkanBackend* vk)
{
    if (!active_)
        return;
    if (awaiting_gpu_capture_ && vk && vk->has_pending_capture())
        return;   // The frame that reads it back has not run yet.
    awaiting_gpu_capture_ = false;

    std::vector<uint8_t> pixels;
    uint32_t             w = 0;
    uint32_t             h = 0;
    if (!vk || !vk->take_surface_capture(pixels, w, h) || w == 0 || h == 0)
    {
        SPECTRA_LOG_ERROR("export",
                          "PNG export failed: the window frame could not be read back ({})",
                          copy_to_clipboard_ ? std::string("clipboard") : path_);
        reset();
        return;
    }

    // An explicit size is honoured by scaling the rendered frame, never by
    // cropping it to (or padding it from) its top-left corner.
    const uint32_t out_w = requested_width_ > 0 ? requested_width_ : w;
    const uint32_t out_h = requested_height_ > 0 ? requested_height_ : h;
    if (out_w != w || out_h != h)
    {
        std::vector<uint8_t> fitted(static_cast<size_t>(out_w) * out_h * 4);
        fit_rgba_image(pixels.data(), w, h, fitted.data(), out_w, out_h);
        pixels = std::move(fitted);
    }

    if (copy_to_clipboard_)
    {
        auto png = ImageExporter::write_png_to_memory(pixels.data(), out_w, out_h);
        if (!png.empty() && platform::copy_image_to_clipboard(png.data(), png.size()))
            SPECTRA_LOG_INFO("export", "Figure copied to clipboard");
        else
            SPECTRA_LOG_WARN("export", "Clipboard image copy failed");
    }
    else if (!path_.empty())
    {
        if (ImageExporter::write_png(path_, pixels.data(), out_w, out_h))
            SPECTRA_LOG_INFO("export", "Saved PNG: " + path_);
        else
            SPECTRA_LOG_ERROR("export", "Failed to write PNG: " + path_);
    }
    reset();
}

bool WindowPngExport::schedule(Figure& fig, VulkanBackend* vk, WindowContext* target_window)
{
    auto& req = fig.export_req_;
    if (active_ || (req.png_path.empty() && !req.copy_to_clipboard))
        return false;

    path_                 = req.png_path;
    theme_                = req.png_theme;
    requested_width_      = req.png_width;
    requested_height_     = req.png_height;
    copy_to_clipboard_    = req.copy_to_clipboard;
    active_               = true;
    awaiting_gpu_capture_ = true;

    req.png_path.clear();
    req.png_theme.clear();
    req.png_width         = 0;
    req.png_height        = 0;
    req.copy_to_clipboard = false;

    if (!vk)
    {
        SPECTRA_LOG_ERROR("export", "Windowed PNG export needs the Vulkan backend");
        reset();
        return false;
    }
    // Size is resolved when the capture runs (see request_surface_capture).
    vk->request_surface_capture(target_window);
    return true;
}

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
WindowContext* find_window_for_figure(WindowManager* window_mgr, uint64_t fig_id)
{
    if (!window_mgr)
        return nullptr;
    for (auto* wctx : window_mgr->windows())
    {
        if (!wctx)
            continue;
        const auto& figs = wctx->assigned_figures;
        if (std::find(figs.begin(), figs.end(), fig_id) != figs.end())
            return wctx;
    }
    return nullptr;
}
#endif

bool write_pending_svg(Figure& f, ui::ThemeManager* theme_mgr)
{
    if (f.export_req_.svg_path.empty())
        return true;
    const std::string path  = std::move(f.export_req_.svg_path);
    const std::string theme = std::move(f.export_req_.svg_theme);
    f.export_req_.svg_path.clear();
    f.export_req_.svg_theme.clear();

    ui::ScopedExportTheme export_theme(theme_mgr, theme);
    f.compute_layout();
    // Same plot surface as the GPU path: the figure's background override (if
    // any) decides ink, letter shades and palette.
    if (theme_mgr)
    {
        const ui::ThemeColors surface = theme_mgr->plot_surface_colors(f.style());
        ui::resolve_auto_series_colors(f,
                                       theme_mgr->plot_surface_data_palette(f.style()),
                                       ui::is_light_plot_background(surface),
                                       ui::effective_figure_background(f.style(), surface));
    }
    const bool ok = SvgExporter::write_svg(
        path,
        f,
        theme_mgr ? ui::svg_style_from_theme(theme_mgr->plot_surface_colors(f.style()))
                  : SvgStyle{});
    if (!ok)
        SPECTRA_LOG_ERROR("app", "Failed to write SVG: {}", path);
    return ok;
}

}   // namespace spectra
