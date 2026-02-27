#pragma once

// ─── Spectra Easy Embed API ─────────────────────────────────────────────────
//
// One-liner offscreen rendering. No windows, no event loop, no boilerplate.
// Renders plots to pixels or files using the GPU (Vulkan headless).
//
//   #include <spectra/easy_embed.hpp>
//
//   int main() {
//       std::vector<float> x = {0, 1, 2, 3, 4};
//       std::vector<float> y = {0, 1, 4, 9, 16};
//
//       // Render to pixel buffer
//       auto img = spectra::render(x, y);
//       // img.data, img.width, img.height, img.stride
//
//       // Save to file (PNG)
//       spectra::render(x, y, "plot.png");
//
//       // With options
//       spectra::render(x, y, {.width = 1920, .height = 1080, .fmt = "r--o",
//                              .title = "My Plot", .save_path = "out.png"});
//
//       // Multiple series
//       spectra::RenderOptions opts;
//       opts.title = "Comparison";
//       opts.save_path = "compare.png";
//       spectra::render({
//           {x, y1, "r-",  "sin(x)"},
//           {x, y2, "b--", "cos(x)"},
//       }, opts);
//
//       // Scatter
//       spectra::render_scatter(x, y, "scatter.png");
//   }
//
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <initializer_list>
#include <span>
#include <spectra/axes.hpp>
#include <spectra/embed.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace spectra
{

// ─── Rendered Image ──────────────────────────────────────────────────────────

struct RenderedImage
{
    std::vector<uint8_t> data;       // RGBA pixels
    uint32_t             width  = 0;
    uint32_t             height = 0;

    // Convenience accessors
    size_t   stride() const { return width * 4; }
    size_t   size_bytes() const { return data.size(); }
    bool     empty() const { return data.empty(); }
    uint8_t* pixels() { return data.data(); }
    const uint8_t* pixels() const { return data.data(); }
};

// ─── Render Options ──────────────────────────────────────────────────────────

struct RenderOptions
{
    uint32_t    width     = 800;
    uint32_t    height    = 600;
    std::string fmt       = "-";       // MATLAB-style format string
    std::string title;
    std::string xlabel;
    std::string ylabel;
    std::string save_path;             // If non-empty, saves PNG to this path
    float       dpi_scale = 1.0f;
    bool        grid      = true;
};

// ─── Series Descriptor (for multi-series render) ─────────────────────────────

struct SeriesDesc
{
    std::span<const float> x;
    std::span<const float> y;
    std::string_view       fmt   = "-";
    std::string_view       label = "";
};

// ─── Internal helpers ────────────────────────────────────────────────────────

namespace detail
{

inline RenderedImage render_to_image(EmbedSurface& surface)
{
    uint32_t w = surface.width();
    uint32_t h = surface.height();
    RenderedImage img;
    img.width  = w;
    img.height = h;
    img.data.resize(w * h * 4);

    if (!surface.render_to_buffer(img.data.data()))
    {
        img.data.clear();
        img.width  = 0;
        img.height = 0;
    }
    return img;
}

inline void configure_axes(Axes& ax, const RenderOptions& opts)
{
    if (!opts.title.empty())
        ax.title(opts.title);
    if (!opts.xlabel.empty())
        ax.xlabel(opts.xlabel);
    if (!opts.ylabel.empty())
        ax.ylabel(opts.ylabel);
    ax.grid(opts.grid);
    ax.auto_fit();
}

inline bool save_if_requested(const RenderedImage& img, const RenderOptions& opts)
{
    if (opts.save_path.empty() || img.empty())
        return false;
    return ImageExporter::write_png(opts.save_path, img.pixels(), img.width, img.height);
}

}   // namespace detail

// ─── One-liner render functions ──────────────────────────────────────────────

// Render a single line plot to pixels.
//
//   auto img = spectra::render(x, y);                       // defaults
//   auto img = spectra::render(x, y, {.width = 1920});      // custom size
//   auto img = spectra::render(x, y, {.save_path = "p.png"}); // save
//   auto img = spectra::render(x, y, {.fmt = "r--o"});      // MATLAB format
//
inline RenderedImage render(std::span<const float> x,
                            std::span<const float> y,
                            const RenderOptions&   opts = {})
{
    EmbedSurface surface(EmbedConfig{opts.width, opts.height, 1, false, opts.dpi_scale});
    auto&        fig = surface.figure();
    auto&        ax  = fig.subplot(1, 1, 1);
    ax.plot(x, y, opts.fmt);
    detail::configure_axes(ax, opts);

    auto img = detail::render_to_image(surface);
    detail::save_if_requested(img, opts);
    return img;
}

// Render multiple series onto a single plot.
//
//   auto img = spectra::render_multi({
//       {x, y1, "r-",  "sin(x)"},
//       {x, y2, "b--", "cos(x)"},
//   });
//
inline RenderedImage render_multi(std::initializer_list<SeriesDesc> series_list,
                                  const RenderOptions&              opts = {})
{
    EmbedSurface surface(EmbedConfig{opts.width, opts.height, 1, false, opts.dpi_scale});
    auto&        fig = surface.figure();
    auto&        ax  = fig.subplot(1, 1, 1);

    for (const auto& sd : series_list)
    {
        auto& line = ax.plot(sd.x, sd.y, sd.fmt);
        if (!sd.label.empty())
            line.label(std::string(sd.label));
    }
    detail::configure_axes(ax, opts);

    auto img = detail::render_to_image(surface);
    detail::save_if_requested(img, opts);
    return img;
}

// Render multiple series from a vector.
inline RenderedImage render_multi(const std::vector<SeriesDesc>& series_list,
                                  const RenderOptions&           opts = {})
{
    EmbedSurface surface(EmbedConfig{opts.width, opts.height, 1, false, opts.dpi_scale});
    auto&        fig = surface.figure();
    auto&        ax  = fig.subplot(1, 1, 1);

    for (const auto& sd : series_list)
    {
        auto& line = ax.plot(sd.x, sd.y, sd.fmt);
        if (!sd.label.empty())
            line.label(std::string(sd.label));
    }
    detail::configure_axes(ax, opts);

    auto img = detail::render_to_image(surface);
    detail::save_if_requested(img, opts);
    return img;
}

// Render a scatter plot.
inline RenderedImage render_scatter(std::span<const float> x,
                                    std::span<const float> y,
                                    const RenderOptions&   opts = {})
{
    EmbedSurface surface(EmbedConfig{opts.width, opts.height, 1, false, opts.dpi_scale});
    auto&        fig = surface.figure();
    auto&        ax  = fig.subplot(1, 1, 1);
    ax.scatter(x, y);
    detail::configure_axes(ax, opts);

    auto img = detail::render_to_image(surface);
    detail::save_if_requested(img, opts);
    return img;
}

// Render a histogram.
inline RenderedImage render_histogram(std::span<const float> values,
                                      int                    bins = 30,
                                      const RenderOptions&   opts = {})
{
    EmbedSurface surface(EmbedConfig{opts.width, opts.height, 1, false, opts.dpi_scale});
    auto&        fig = surface.figure();
    auto&        ax  = fig.subplot(1, 1, 1);
    ax.histogram(values, bins);
    detail::configure_axes(ax, opts);

    auto img = detail::render_to_image(surface);
    detail::save_if_requested(img, opts);
    return img;
}

// Render a bar chart.
inline RenderedImage render_bar(std::span<const float> positions,
                                std::span<const float> heights,
                                const RenderOptions&   opts = {})
{
    EmbedSurface surface(EmbedConfig{opts.width, opts.height, 1, false, opts.dpi_scale});
    auto&        fig = surface.figure();
    auto&        ax  = fig.subplot(1, 1, 1);
    ax.bar(positions, heights);
    detail::configure_axes(ax, opts);

    auto img = detail::render_to_image(surface);
    detail::save_if_requested(img, opts);
    return img;
}

}   // namespace spectra
