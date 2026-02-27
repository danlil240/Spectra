#include <spectra/spectra_embed_c.h>
#include <spectra/embed.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/series.hpp>

#include <cstring>
#include <span>
#include <string>
#include <vector>

// Opaque handle wrappers — the C API uses pointers to these.
// EmbedSurface owns all figures/axes/series, so we just wrap pointers.

struct SpectraEmbed
{
    spectra::EmbedSurface surface;
    explicit SpectraEmbed(uint32_t w, uint32_t h) : surface(spectra::EmbedConfig{w, h}) {}
};

struct SpectraFigure
{
    spectra::Figure* ptr;
};

struct SpectraAxes
{
    spectra::Axes*     axes_2d = nullptr;
    spectra::Axes3D*   axes_3d = nullptr;
    spectra::AxesBase* base    = nullptr;
};

struct SpectraSeries
{
    spectra::Series* ptr;
};

// Small pool of wrapper objects so the C API can return stable pointers.
// These are leaked intentionally — the embed surface owns the real objects.
// A real production API would use a handle table; this is sufficient for FFI demos.
static thread_local std::vector<SpectraFigure> g_fig_pool;
static thread_local std::vector<SpectraAxes>   g_ax_pool;
static thread_local std::vector<SpectraSeries> g_series_pool;

extern "C" {

// ── Lifecycle ───────────────────────────────────────────────────────────────

SpectraEmbed* spectra_embed_create(uint32_t width, uint32_t height)
{
    auto* s = new (std::nothrow) SpectraEmbed(width, height);
    if (!s || !s->surface.is_valid())
    {
        delete s;
        return nullptr;
    }
    return s;
}

void spectra_embed_destroy(SpectraEmbed* s)
{
    delete s;
}

int spectra_embed_is_valid(const SpectraEmbed* s)
{
    return (s && s->surface.is_valid()) ? 1 : 0;
}

// ── Figure management ───────────────────────────────────────────────────────

SpectraFigure* spectra_embed_figure(SpectraEmbed* s)
{
    if (!s)
        return nullptr;
    auto& fig = s->surface.figure();
    g_fig_pool.push_back(SpectraFigure{&fig});
    return &g_fig_pool.back();
}

SpectraFigure* spectra_embed_active_figure(SpectraEmbed* s)
{
    if (!s)
        return nullptr;
    auto* fig = s->surface.active_figure();
    if (!fig)
        return nullptr;
    g_fig_pool.push_back(SpectraFigure{fig});
    return &g_fig_pool.back();
}

// ── Axes management ─────────────────────────────────────────────────────────

SpectraAxes* spectra_figure_subplot(SpectraFigure* fig, int rows, int cols, int index)
{
    if (!fig || !fig->ptr)
        return nullptr;
    auto& ax = fig->ptr->subplot(rows, cols, index);
    g_ax_pool.push_back(SpectraAxes{&ax, nullptr, &ax});
    return &g_ax_pool.back();
}

SpectraAxes* spectra_figure_subplot3d(SpectraFigure* fig, int rows, int cols, int index)
{
    if (!fig || !fig->ptr)
        return nullptr;
    auto& ax = fig->ptr->subplot3d(rows, cols, index);
    g_ax_pool.push_back(SpectraAxes{nullptr, &ax, &ax});
    return &g_ax_pool.back();
}

// ── Series creation ─────────────────────────────────────────────────────────

SpectraSeries* spectra_axes_line(SpectraAxes* ax, const float* x, const float* y,
                                  uint32_t count, const char* label)
{
    if (!ax || !ax->axes_2d || !x || !y || count == 0)
        return nullptr;

    std::span<const float> xs(x, count);
    std::span<const float> ys(y, count);
    auto& series = ax->axes_2d->line(xs, ys);
    if (label && label[0] != '\0')
        series.label(label);

    g_series_pool.push_back(SpectraSeries{&series});
    return &g_series_pool.back();
}

SpectraSeries* spectra_axes_scatter(SpectraAxes* ax, const float* x, const float* y,
                                     uint32_t count, const char* label)
{
    if (!ax || !ax->axes_2d || !x || !y || count == 0)
        return nullptr;

    std::span<const float> xs(x, count);
    std::span<const float> ys(y, count);
    auto& series = ax->axes_2d->scatter(xs, ys);
    if (label && label[0] != '\0')
        series.label(label);

    g_series_pool.push_back(SpectraSeries{&series});
    return &g_series_pool.back();
}

// ── Series data update ──────────────────────────────────────────────────────

void spectra_series_set_x(SpectraSeries* s, const float* x, uint32_t count)
{
    if (!s || !s->ptr || !x || count == 0)
        return;
    if (auto* line = dynamic_cast<spectra::LineSeries*>(s->ptr))
        line->set_x({x, count});
    else if (auto* sc = dynamic_cast<spectra::ScatterSeries*>(s->ptr))
        sc->set_x({x, count});
}

void spectra_series_set_y(SpectraSeries* s, const float* y, uint32_t count)
{
    if (!s || !s->ptr || !y || count == 0)
        return;
    if (auto* line = dynamic_cast<spectra::LineSeries*>(s->ptr))
        line->set_y({y, count});
    else if (auto* sc = dynamic_cast<spectra::ScatterSeries*>(s->ptr))
        sc->set_y({y, count});
}

// ── Rendering ───────────────────────────────────────────────────────────────

int spectra_embed_render(SpectraEmbed* s, uint8_t* out_rgba)
{
    if (!s || !out_rgba)
        return 0;
    return s->surface.render_to_buffer(out_rgba) ? 1 : 0;
}

int spectra_embed_resize(SpectraEmbed* s, uint32_t width, uint32_t height)
{
    if (!s)
        return 0;
    return s->surface.resize(width, height) ? 1 : 0;
}

uint32_t spectra_embed_width(const SpectraEmbed* s)
{
    return s ? s->surface.width() : 0;
}

uint32_t spectra_embed_height(const SpectraEmbed* s)
{
    return s ? s->surface.height() : 0;
}

// ── Input forwarding ────────────────────────────────────────────────────────

void spectra_embed_mouse_move(SpectraEmbed* s, float x, float y)
{
    if (s)
        s->surface.inject_mouse_move(x, y);
}

void spectra_embed_mouse_button(SpectraEmbed* s, int button, int action, int mods,
                                 float x, float y)
{
    if (s)
        s->surface.inject_mouse_button(button, action, mods, x, y);
}

void spectra_embed_scroll(SpectraEmbed* s, float dx, float dy, float cx, float cy)
{
    if (s)
        s->surface.inject_scroll(dx, dy, cx, cy);
}

void spectra_embed_key(SpectraEmbed* s, int key, int action, int mods)
{
    if (s)
        s->surface.inject_key(key, action, mods);
}

void spectra_embed_update(SpectraEmbed* s, float dt)
{
    if (s)
        s->surface.update(dt);
}

// ── Easy Render API ──────────────────────────────────────────────────────────

// Internal helper: create surface, add series, render to heap-allocated buffer.
static uint8_t* render_easy(const float* x, const float* y, uint32_t count,
                             uint32_t width, uint32_t height,
                             uint32_t* out_width, uint32_t* out_height,
                             bool scatter)
{
    if (!x || !y || count == 0 || width == 0 || height == 0)
        return nullptr;

    spectra::EmbedSurface surface(spectra::EmbedConfig{width, height});
    if (!surface.is_valid())
        return nullptr;

    auto& fig = surface.figure();
    auto& ax  = fig.subplot(1, 1, 1);

    std::span<const float> xs(x, count);
    std::span<const float> ys(y, count);

    if (scatter)
        ax.scatter(xs, ys);
    else
        ax.line(xs, ys);

    ax.auto_fit();

    auto* buf = new (std::nothrow) uint8_t[width * height * 4];
    if (!buf)
        return nullptr;

    if (!surface.render_to_buffer(buf))
    {
        delete[] buf;
        return nullptr;
    }

    if (out_width)
        *out_width = width;
    if (out_height)
        *out_height = height;
    return buf;
}

uint8_t* spectra_render_line(const float* x, const float* y, uint32_t count,
                              uint32_t width, uint32_t height,
                              uint32_t* out_width, uint32_t* out_height)
{
    return render_easy(x, y, count, width, height, out_width, out_height, false);
}

uint8_t* spectra_render_scatter(const float* x, const float* y, uint32_t count,
                                 uint32_t width, uint32_t height,
                                 uint32_t* out_width, uint32_t* out_height)
{
    return render_easy(x, y, count, width, height, out_width, out_height, true);
}

int spectra_render_line_png(const float* x, const float* y, uint32_t count,
                             uint32_t width, uint32_t height,
                             const char* path)
{
    if (!path)
        return 0;
    uint32_t w = 0, h = 0;
    uint8_t* buf = render_easy(x, y, count, width, height, &w, &h, false);
    if (!buf)
        return 0;
    bool ok = spectra::ImageExporter::write_png(path, buf, w, h);
    delete[] buf;
    return ok ? 1 : 0;
}

int spectra_render_scatter_png(const float* x, const float* y, uint32_t count,
                                uint32_t width, uint32_t height,
                                const char* path)
{
    if (!path)
        return 0;
    uint32_t w = 0, h = 0;
    uint8_t* buf = render_easy(x, y, count, width, height, &w, &h, true);
    if (!buf)
        return 0;
    bool ok = spectra::ImageExporter::write_png(path, buf, w, h);
    delete[] buf;
    return ok ? 1 : 0;
}

void spectra_free_pixels(uint8_t* pixels)
{
    delete[] pixels;
}

}   // extern "C"
