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
#include <optional>
#include <span>
#include <spectra/axes.hpp>
#include <spectra/embed.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace spectra
{

// ─── Rendered Image ──────────────────────────────────────────────────────────

struct RenderedImage
{
    std::vector<uint8_t> data;   // RGBA pixels
    uint32_t             width  = 0;
    uint32_t             height = 0;

    // Convenience accessors
    size_t         stride() const { return width * 4; }
    size_t         size_bytes() const { return data.size(); }
    bool           empty() const { return data.empty(); }
    uint8_t*       pixels() { return data.data(); }
    const uint8_t* pixels() const { return data.data(); }
};

// ─── Render Options ──────────────────────────────────────────────────────────

struct RenderOptions
{
    uint32_t    width  = 800;
    uint32_t    height = 600;
    std::string fmt    = "-";   // MATLAB-style format string
    std::string title;
    std::string xlabel;
    std::string ylabel;
    std::string save_path;   // If non-empty, saves PNG to this path
    std::string theme;       // Theme name: "dark", "night", or "light". Default: "night".
    float       dpi_scale        = 1.0f;
    uint32_t    msaa             = 1;      // MSAA samples (1 = off, 4 = 4×)
    bool        grid             = true;
    float       background_alpha = 1.0f;   // 0 = transparent background

    // Legend (Phase 6A)
    bool           legend     = false;
    LegendPosition legend_pos = LegendPosition::TopRight;

    // Subplot grid (Phase 6A) — used by render_subplots().
    int subplot_rows = 1;
    int subplot_cols = 1;

    // Optional manual axis limits (Phase 6A).
    std::optional<std::pair<double, double>> xlim;
    std::optional<std::pair<double, double>> ylim;
};

// ─── Series Descriptor (for multi-series render) ─────────────────────────────

struct SeriesDesc
{
    std::span<const float> x;
    std::span<const float> y;
    std::string_view       fmt   = "-";
    std::string_view       label = "";

    // Per-series style overrides (Phase 6B). When set, they override the
    // values implied by the format string.
    std::optional<Color> color;        // RGBA override
    float                line_width  = 0.0f;   // 0 = leave default
    float                marker_size = 0.0f;   // 0 = leave default
    float                opacity     = 1.0f;   // 1 = opaque
};

// ─── Internal helpers ────────────────────────────────────────────────────────

namespace detail
{

inline EmbedConfig make_embed_config(const RenderOptions& opts)
{
    EmbedConfig cfg;
    cfg.width            = opts.width;
    cfg.height           = opts.height;
    cfg.msaa             = opts.msaa > 0 ? opts.msaa : 1;
    cfg.dpi_scale        = opts.dpi_scale;
    cfg.background_alpha = opts.background_alpha;
    cfg.show_legend      = opts.legend;
    if (!opts.theme.empty())
        cfg.theme = opts.theme;
    return cfg;
}

// Apply figure-level options (legend visibility/position).
inline void configure_figure(Figure& fig, const RenderOptions& opts)
{
    fig.legend().visible  = opts.legend;
    fig.legend().position = opts.legend_pos;
}

// Apply per-series style overrides from a SeriesDesc.
inline void apply_series_style(LineSeries& line, const SeriesDesc& sd)
{
    if (sd.color)
        line.color(*sd.color);
    if (sd.line_width > 0.0f)
        line.width(sd.line_width);
    if (sd.marker_size > 0.0f)
        line.marker_size(sd.marker_size);
    if (sd.opacity < 1.0f)
        line.opacity(sd.opacity);
}

inline RenderedImage render_to_image(EmbedSurface& surface)
{
    uint32_t      w = surface.width();
    uint32_t      h = surface.height();
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
    if (opts.xlim)
        ax.xlim(opts.xlim->first, opts.xlim->second);
    if (opts.ylim)
        ax.ylim(opts.ylim->first, opts.ylim->second);
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
    EmbedSurface surface(detail::make_embed_config(opts));
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
    EmbedSurface surface(detail::make_embed_config(opts));
    auto&        fig = surface.figure();
    auto&        ax  = fig.subplot(1, 1, 1);

    for (const auto& sd : series_list)
    {
        auto& line = ax.plot(sd.x, sd.y, sd.fmt);
        if (!sd.label.empty())
            line.label(std::string(sd.label));
        detail::apply_series_style(line, sd);
    }
    detail::configure_axes(ax, opts);
    detail::configure_figure(fig, opts);

    auto img = detail::render_to_image(surface);
    detail::save_if_requested(img, opts);
    return img;
}

// Render multiple series from a vector.
inline RenderedImage render_multi(const std::vector<SeriesDesc>& series_list,
                                  const RenderOptions&           opts = {})
{
    EmbedSurface surface(detail::make_embed_config(opts));
    auto&        fig = surface.figure();
    auto&        ax  = fig.subplot(1, 1, 1);

    for (const auto& sd : series_list)
    {
        auto& line = ax.plot(sd.x, sd.y, sd.fmt);
        if (!sd.label.empty())
            line.label(std::string(sd.label));
        detail::apply_series_style(line, sd);
    }
    detail::configure_axes(ax, opts);
    detail::configure_figure(fig, opts);

    auto img = detail::render_to_image(surface);
    detail::save_if_requested(img, opts);
    return img;
}

// Render a scatter plot.
inline RenderedImage render_scatter(std::span<const float> x,
                                    std::span<const float> y,
                                    const RenderOptions&   opts = {})
{
    EmbedSurface surface(detail::make_embed_config(opts));
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
    EmbedSurface surface(detail::make_embed_config(opts));
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
    EmbedSurface surface(detail::make_embed_config(opts));
    auto&        fig = surface.figure();
    auto&        ax  = fig.subplot(1, 1, 1);
    ax.bar(positions, heights);
    detail::configure_axes(ax, opts);

    auto img = detail::render_to_image(surface);
    detail::save_if_requested(img, opts);
    return img;
}

// ─── Subplot grid render (Phase 6C) ──────────────────────────────────────────

// A single cell in a subplot grid: one or more series plus per-cell labels.
struct SubplotDesc
{
    std::vector<SeriesDesc> series;
    std::string             title;
    std::string             xlabel;
    std::string             ylabel;
};

// Render a grid of subplots.  `subplots` is laid out row-major; cells beyond
// the supplied list are left empty.  opts.subplot_rows/cols are overridden by
// the explicit `rows`/`cols` arguments.
inline RenderedImage render_subplots(int                            rows,
                                     int                            cols,
                                     const std::vector<SubplotDesc>& subplots,
                                     const RenderOptions&            opts = {})
{
    EmbedSurface surface(detail::make_embed_config(opts));
    auto&        fig = surface.figure();

    if (rows < 1)
        rows = 1;
    if (cols < 1)
        cols = 1;

    int idx = 0;
    for (const auto& cell : subplots)
    {
        if (idx >= rows * cols)
            break;
        auto& ax = fig.subplot(rows, cols, idx + 1);
        for (const auto& sd : cell.series)
        {
            auto& line = ax.plot(sd.x, sd.y, sd.fmt);
            if (!sd.label.empty())
                line.label(std::string(sd.label));
            detail::apply_series_style(line, sd);
        }
        if (!cell.title.empty())
            ax.title(cell.title);
        if (!cell.xlabel.empty())
            ax.xlabel(cell.xlabel);
        if (!cell.ylabel.empty())
            ax.ylabel(cell.ylabel);
        ax.grid(opts.grid);
        ax.auto_fit();
        ++idx;
    }

    detail::configure_figure(fig, opts);

    auto img = detail::render_to_image(surface);
    detail::save_if_requested(img, opts);
    return img;
}

}   // namespace spectra
