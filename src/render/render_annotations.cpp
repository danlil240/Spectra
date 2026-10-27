// render_annotations.cpp — screen-space legend and colorbar.
//
// The interactive window draws its legend with the ImGui LegendInteraction
// overlay (drag, click-to-toggle).  Headless export, canvas-only embeds and
// any other path without that overlay get the legend from here instead
// (see Renderer::set_legend_overlay_active), built from quads, SDF line /
// marker swatches and TextRenderer text.  Placement uses the same
// legend_layout helpers as the ImGui legend so window and export agree.
//
// Colorbars (AxesBase::show_colorbar) have no overlay version and are always
// drawn here, in the strip that AxesBase::set_viewport reserves for them.

#include <algorithm>
#include <cmath>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/chunked_series.hpp>
#include <spectra/figure.hpp>
#include <spectra/plot_style.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_stats.hpp>

#include "core/colorbar_layout.hpp"
#include "renderer.hpp"
#include "scatter_marker.hpp"
#include "ui/overlay/legend_layout.hpp"
#include "ui/theme/theme.hpp"

namespace spectra
{

namespace
{

// Legend entry geometry (pixels).  The swatch column is wide enough to show a
// line's dash pattern; markers and fill squares are centred in it.
constexpr float SWATCH_W      = 22.0f;
constexpr float SWATCH_GAP    = 6.0f;
constexpr float FILL_SWATCH   = 10.0f;
constexpr float CORNER_RADIUS = 4.0f;
constexpr float HIDDEN_ALPHA  = 0.15f;   // same dimming as the ImGui legend

float srgb_to_linear(float c)
{
    return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Theme colours go through the same optional linearisation as the rest of
// the plot chrome (ThemeColors::linearize_colors).
Color theme_color(const ui::Color& c, const ui::ThemeManager& tm, float alpha_scale = 1.0f)
{
    if (tm.current().linearize_colors)
        return {srgb_to_linear(c.r), srgb_to_linear(c.g), srgb_to_linear(c.b), c.a * alpha_scale};
    return {c.r, c.g, c.b, c.a * alpha_scale};
}

uint32_t pack_rgba(const Color& c)
{
    auto ch = [](float v)
    { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return ch(c.r) | (ch(c.g) << 8) | (ch(c.b) << 16) | (ch(c.a) << 24);
}

// Legend text size.  An explicit LegendConfig::font_size is drawn at exactly
// that pixel size (a baked preset scaled by TextRenderer) with rows of
// font_size + 6 px, the same metrics as the window legend
// (LegendInteraction) and the SVG exporter.  Unset (0) keeps the historical
// Label preset with rows sized from its line height (golden baselines).
struct LegendFont
{
    FontSize preset   = FontSize::Label;
    float    scale    = 1.0f;
    float    pixel_px = 0.0f;   // requested size; 0 = auto
};

LegendFont legend_font(float font_size)
{
    if (!(font_size > 0.0f))
        return {};
    const auto sf = TextRenderer::scaled_font(font_size);
    return {sf.preset, sf.scale, font_size};
}

enum class SwatchKind
{
    Line,     // short line in the series' line style (+ marker if it has one)
    Marker,   // a single marker
    Fill,     // filled square (bars, histograms, bands, box/violin, shapes)
};

SwatchKind swatch_kind(const Series& s)
{
    if (dynamic_cast<const ScatterSeries*>(&s) || dynamic_cast<const ScatterSeries3D*>(&s))
        return SwatchKind::Marker;
    if (dynamic_cast<const LineSeries3D*>(&s))
        return SwatchKind::Line;
    if (const auto* surf = dynamic_cast<const SurfaceSeries*>(&s); surf && surf->wireframe())
        return SwatchKind::Line;
    if (dynamic_cast<const LineSeries*>(&s) || dynamic_cast<const ChunkedLineSeries*>(&s)
        || dynamic_cast<const StemSeries*>(&s))
    {
        const auto& st = s.plot_style();
        return (st.has_line() || !st.has_marker()) ? SwatchKind::Line : SwatchKind::Marker;
    }
    return SwatchKind::Fill;
}

float series_line_width(const Series& s)
{
    if (const auto* l = dynamic_cast<const LineSeries*>(&s))
        return l->width();
    if (const auto* c = dynamic_cast<const ChunkedLineSeries*>(&s))
        return c->width();
    if (const auto* l3 = dynamic_cast<const LineSeries3D*>(&s))
        return l3->width();
    if (dynamic_cast<const SurfaceSeries*>(&s))
        return 1.5f;   // wireframe
    return 2.0f;
}

}   // namespace

void Renderer::render_annotations(Figure& figure)
{
    annotation_draws_.clear();
    annotation_vertex_scratch_.clear();
    annotation_point_scratch_.clear();
    legend_overflow_cells_.clear();

    const float fh    = static_cast<float>(figure.height());
    annotation_fig_w_ = static_cast<float>(figure.width());
    annotation_fig_h_ = fh;

    // Colorbars: figure.axes() holds the 2D axes, figure.all_axes() the 3D ones.
    auto colorbar_for = [&](AxesBase* axes)
    {
        if (!axes)
            return;
        const Rect& vp = axes->viewport();
        if (vp.y + vp.h < 0.0f || vp.y > fh)
            return;   // scrolled off-screen
        if (colorbar_layout::wants_colorbar(*axes))
            build_colorbar(*axes);
    };
    for (auto& axes_ptr : figure.axes())
        colorbar_for(axes_ptr.get());
    for (auto& axes_ptr : figure.all_axes())
        colorbar_for(axes_ptr.get());

    const LegendConfig& legend = figure.legend();
    if (!legend_overlay_active_ && legend.visible && legend.position != LegendPosition::None)
    {
        for (auto& axes_ptr : figure.axes())
        {
            if (!axes_ptr)
                continue;
            const Rect& vp = axes_ptr->viewport();
            if (vp.y + vp.h < 0.0f || vp.y > fh)
                continue;
            build_legend(*axes_ptr, legend);
        }
        // 3D axes live in all_axes() (same order as the SVG exporter).
        for (auto& axes_ptr : figure.all_axes())
        {
            if (!axes_ptr)
                continue;
            const Rect& vp = axes_ptr->viewport();
            if (vp.y + vp.h < 0.0f || vp.y > fh)
                continue;
            build_legend(*axes_ptr, legend);
        }
    }

    if (annotation_draws_.empty())
        return;

    // ── Upload this figure's annotation geometry (per-figure, per-slot) ──
    const uint32_t slot    = backend_.current_flight_frame() % FRAME_BUFFER_SLOTS;
    FigureGpuData& fig_gpu = figure_gpu_data_[&figure];
    auto           upload =
        [this](BufferHandle& buf, size_t& cap, BufferUsage usage, const std::vector<float>& data)
    {
        if (data.empty())
            return;
        const size_t bytes = data.size() * sizeof(float);
        if (!buf || cap < bytes)
        {
            if (buf)
                backend_.destroy_buffer(buf);
            buf = backend_.create_buffer(usage, bytes * 2);
            cap = bytes * 2;
        }
        backend_.upload_buffer(buf, data.data(), bytes);
    };
    upload(fig_gpu.annotation_vertex_buffer[slot],
           fig_gpu.annotation_vertex_capacity[slot],
           BufferUsage::Vertex,
           annotation_vertex_scratch_);
    upload(fig_gpu.annotation_point_buffer[slot],
           fig_gpu.annotation_point_capacity[slot],
           BufferUsage::Storage,
           annotation_point_scratch_);

    bind_screen_space_ubo(static_cast<float>(figure.width()), fh);

    for (const auto& d : annotation_draws_)
    {
        switch (d.kind)
        {
            case AnnotationDraw::Kind::Triangles:
            case AnnotationDraw::Kind::Lines:
                backend_.bind_pipeline(d.kind == AnnotationDraw::Kind::Triangles ? overlay_pipeline_
                                                                                 : grid_pipeline_);
                backend_.push_constants(d.pc);
                backend_.bind_buffer(fig_gpu.annotation_vertex_buffer[slot], 0);
                backend_.draw(d.count, d.first);
                break;
            case AnnotationDraw::Kind::Segment:
                backend_.bind_pipeline(line_pipeline_);
                backend_.push_constants(d.pc);
                backend_.bind_buffer(fig_gpu.annotation_point_buffer[slot], 0);
                backend_.draw(6, d.first * 6);
                break;
            case AnnotationDraw::Kind::Marker:
                backend_.bind_pipeline(scatter_pipeline_);
                backend_.push_constants(d.pc);
                backend_.bind_buffer(fig_gpu.annotation_point_buffer[slot], 0);
                backend_.draw_instanced(6, 1, 0, d.first);
                break;
        }
    }
}

namespace
{

// Small builders appending vec2 geometry and recording the matching draw.
struct AnnotationBuilder
{
    std::vector<float>& verts;
    std::vector<float>& points;

    uint32_t vertex_count() const { return static_cast<uint32_t>(verts.size() / 2); }
    uint32_t point_count() const { return static_cast<uint32_t>(points.size() / 2); }

    void quad(float x0, float y0, float x1, float y1)
    {
        const float v[] = {x0, y0, x1, y0, x0, y1, x0, y1, x1, y0, x1, y1};
        verts.insert(verts.end(), std::begin(v), std::end(v));
    }

    // Closed rounded-rectangle outline (clockwise in screen space).
    static std::vector<std::pair<float, float>> rounded_outline(const Rect& r, float radius)
    {
        constexpr int SEGS  = 4;   // per quarter circle
        radius              = std::max(0.0f, std::min({radius, r.w * 0.5f, r.h * 0.5f}));
        const float cx[]    = {r.x + r.w - radius, r.x + r.w - radius, r.x + radius, r.x + radius};
        const float cy[]    = {r.y + radius, r.y + r.h - radius, r.y + r.h - radius, r.y + radius};
        const float start[] = {-0.5f, 0.0f, 0.5f, 1.0f};   // in units of pi
        std::vector<std::pair<float, float>> pts;
        for (int c = 0; c < 4; ++c)
        {
            for (int i = 0; i <= SEGS; ++i)
            {
                const float a =
                    (start[c] + 0.5f * static_cast<float>(i) / SEGS) * 3.14159265358979f;
                pts.emplace_back(cx[c] + radius * std::cos(a), cy[c] + radius * std::sin(a));
            }
        }
        return pts;
    }

    void rounded_fill(const Rect& r, float radius)
    {
        const auto  pts = rounded_outline(r, radius);
        const float mx  = r.x + r.w * 0.5f;
        const float my  = r.y + r.h * 0.5f;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const auto& a   = pts[i];
            const auto& b   = pts[(i + 1) % pts.size()];
            const float v[] = {mx, my, a.first, a.second, b.first, b.second};
            verts.insert(verts.end(), std::begin(v), std::end(v));
        }
    }

    void rounded_stroke(const Rect& r, float radius)
    {
        const auto pts = rounded_outline(r, radius);
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const auto& a   = pts[i];
            const auto& b   = pts[(i + 1) % pts.size()];
            const float v[] = {a.first, a.second, b.first, b.second};
            verts.insert(verts.end(), std::begin(v), std::end(v));
        }
    }

    void line(float x0, float y0, float x1, float y1)
    {
        const float v[] = {x0, y0, x1, y1};
        verts.insert(verts.end(), std::begin(v), std::end(v));
    }

    uint32_t point(float x, float y)
    {
        const uint32_t idx = point_count();
        points.push_back(x);
        points.push_back(y);
        return idx;
    }

    // Append a line-pipeline segment A→B and return its segment index.
    // line.vert derives the dash phase from the summed length of up to 512
    // preceding segments in the buffer, which here belong to other swatches.
    // A padding point Q (A + detour along the incoming direction) is inserted
    // so that summed length is a whole number of pattern repeats and every
    // dashed swatch starts with a full dash.
    uint32_t dash_aligned_segment(float ax, float ay, float bx, float by, float pattern_total)
    {
        const uint32_t n = point_count();
        if (n == 0 || !(pattern_total > 0.0f))
        {
            const uint32_t idx = point(ax, ay);
            point(bx, by);
            return idx;
        }
        constexpr uint32_t WALK_LIMIT = 512;     // must match line.vert
        const uint32_t     a_idx      = n + 1;   // after the padding point
        const uint32_t     walk_from  = a_idx > WALK_LIMIT ? a_idx - WALK_LIMIT : 0;
        auto               seg_len    = [this](uint32_t i)
        {
            const float dx = points[2 * (i + 1)] - points[2 * i];
            const float dy = points[2 * (i + 1) + 1] - points[2 * i + 1];
            return std::sqrt(dx * dx + dy * dy);
        };
        float walked = 0.0f;   // existing segments inside the walk window
        for (uint32_t i = walk_from; i + 1 < n; ++i)
            walked += seg_len(i);
        const float px     = points[2 * (n - 1)];
        const float py     = points[2 * (n - 1) + 1];
        const float dx     = ax - px;
        const float dy     = ay - py;
        const float direct = std::sqrt(dx * dx + dy * dy);
        const float extra =
            std::fmod(pattern_total - std::fmod(walked + direct, pattern_total), pattern_total);
        // Q lies extra/2 beyond A on the P→A line: |PQ| + |QA| = direct + extra.
        const float ux = direct > 1e-4f ? dx / direct : 1.0f;
        const float uy = direct > 1e-4f ? dy / direct : 0.0f;
        point(ax + ux * extra * 0.5f, ay + uy * extra * 0.5f);
        point(ax, ay);
        point(bx, by);
        return a_idx;
    }
};

void set_color(SeriesPushConstants& pc, const Color& c)
{
    pc.color[0] = c.r;
    pc.color[1] = c.g;
    pc.color[2] = c.b;
    pc.color[3] = c.a;
}

}   // namespace

void Renderer::build_legend(AxesBase& axes, const LegendConfig& config)
{
    if (!text_renderer_.is_initialized())
        return;

    const std::vector<Series*> entries = legend_layout::legend_entries(axes);
    if (entries.empty())
        return;

    const auto& tc    = plot_colors();
    const Rect& vp    = axes.viewport();
    const Rect  area  = legend_layout::legend_area(axes, config.position);
    const float pad_x = config.padding + 2.0f;   // as LegendInteraction
    const float pad_y = config.padding;

    // A static image cannot scroll, so every entry must be represented: see
    // legend_layout::compute_static_grid for the policy (tighter rows, then
    // an explicit "+N more" cell).  Shrink-to-fit rule shared with the SVG
    // exporter (see legend_layout::compute_static_grid): only the automatic
    // size may fall back to the smaller tick font before entries are
    // summarised; an explicitly requested font_size is always honoured.
    struct Fit
    {
        LegendFont                font;
        legend_layout::GridLayout grid;
    };
    auto fit_with = [&](const LegendFont& f)
    {
        float max_label_w = 0.0f;
        float th          = f.pixel_px;
        for (const Series* s : entries)
        {
            const auto ext = text_renderer_.measure_text(s->label(), f.preset, f.scale);
            max_label_w    = std::max(max_label_w, ext.width);
            if (f.pixel_px <= 0.0f)
                th = std::max(th, ext.height);
        }
        const int   n = static_cast<int>(entries.size());
        const float overflow_w =
            text_renderer_.measure_text(legend_layout::overflow_text(n), f.preset, f.scale).width;
        const float cw = SWATCH_W + SWATCH_GAP + max_label_w;
        const auto  g =
            legend_layout::compute_static_grid(n,
                                               cw,
                                               th + 6.0f,
                                               th + 2.0f,
                                               overflow_w,
                                               pad_x,
                                               pad_y,
                                               legend_layout::COL_GAP,
                                               legend_layout::max_box_w(area, config.position),
                                               legend_layout::max_box_h(area, config.position));
        return Fit{f, g};
    };
    Fit fit = fit_with(legend_font(config.font_size));
    if (fit.grid.overflow > 0 && fit.font.pixel_px <= 0.0f && fit.font.preset != FontSize::Tick)
    {
        Fit small = fit_with(LegendFont{FontSize::Tick, 1.0f, 0.0f});
        if (small.grid.shown > fit.grid.shown)
            fit = small;
    }
    const FontSize font       = fit.font.preset;
    const float    font_scale = fit.font.scale;
    const auto&    grid       = fit.grid;
    const float    row_h      = grid.cell_h;
    const float    col_w      = grid.cell_w;
    if (grid.rows <= 0)
        return;

    auto                 it   = legend_best_corner_.find(&axes);
    const LegendPosition prev = it != legend_best_corner_.end() ? it->second : LegendPosition::None;
    const LegendPosition corner =
        legend_layout::resolve_position(axes, vp, grid.width, grid.height, config.position, prev);
    if (config.position == LegendPosition::Best)
        legend_best_corner_[&axes] = corner;
    const Rect region = corner == LegendPosition::OutsideRight ? area : vp;
    if (!legend_layout::fits(region, grid.width, grid.height, corner))
        return;   // not even one row fits this (tiny) axes
    const Rect box = legend_layout::place(region, corner, grid.width, grid.height);

    // Labels longer than their column are ellipsized (UTF-8 safe) so the
    // legend never runs past its axes (audit S04).  The 0.5 px slack absorbs
    // float error from re-deriving the column width, so the widest label
    // that sized the column is never cut.
    const float label_w = legend_layout::column_width(grid, pad_x, legend_layout::COL_GAP)
                          - SWATCH_W - SWATCH_GAP + 0.5f;
    const char* ellipsis = text_renderer_.has_glyph(0x2026, font) ? "\xE2\x80\xA6" : "...";
    auto        measure  = [this, font_scale](const std::string& t, FontSize fs)
    { return text_renderer_.measure_text(t, fs, font_scale).width; };

    AnnotationBuilder b{annotation_vertex_scratch_, annotation_point_scratch_};
    auto              add = [this](AnnotationDraw::Kind       k,
                      uint32_t                   first,
                      uint32_t                   count,
                      const SeriesPushConstants& pc)
    { annotation_draws_.push_back({k, first, count, pc}); };

    // ── Box: background + border ──
    {
        // LegendConfig::uses_*_color: black RGB without the has_* flag = theme colour.
        const Color bg =
            config.uses_bg_color() ? config.bg_color : theme_color(tc.tooltip_bg, theme_mgr_, 0.9f);
        const Color         border = config.uses_border_color() ? config.border_color
                                                                : theme_color(tc.border_subtle, theme_mgr_);
        SeriesPushConstants pc{};
        set_color(pc, bg);
        uint32_t first = b.vertex_count();
        b.rounded_fill(box, CORNER_RADIUS);
        add(AnnotationDraw::Kind::Triangles, first, b.vertex_count() - first, pc);

        set_color(pc, border);
        pc.line_width = 1.0f;
        first         = b.vertex_count();
        b.rounded_stroke(box, CORNER_RADIUS);
        add(AnnotationDraw::Kind::Lines, first, b.vertex_count() - first, pc);
    }

    // ── Entries (column-major, like LegendInteraction) ──
    const float bg_luma =
        0.2126f * tc.bg_canvas.r + 0.7152f * tc.bg_canvas.g + 0.0722f * tc.bg_canvas.b;
    const Color text_base = theme_color(tc.text_secondary, theme_mgr_);
    for (size_t i = 0; i < static_cast<size_t>(grid.shown); ++i)
    {
        const int     row = static_cast<int>(i) % grid.rows;
        const int     col = static_cast<int>(i) / grid.rows;
        const Series& s   = *entries[i];
        const float x  = box.x + pad_x + static_cast<float>(col) * (col_w + legend_layout::COL_GAP);
        const float y  = box.y + pad_y + static_cast<float>(row) * row_h;
        const float cy = y + row_h * 0.5f;

        const float vis_alpha = s.visible() ? 1.0f : HIDDEN_ALPHA;
        Color       sc        = ui::adapt_series_color_to_theme(legend_layout::swatch_color(s), tc);
        if (const auto* scat = dynamic_cast<const ScatterSeries*>(&s); scat && scat->has_colormap())
            sc = colorbar_layout::sample(scat->colormap(), colorbar_layout::Mapping::Scatter, 0.6f);
        sc.a *= s.opacity() * vis_alpha;

        const auto&         st = s.plot_style();
        SeriesPushConstants pc{};
        set_color(pc, sc);
        pc.opacity = 1.0f;

        switch (swatch_kind(s))
        {
            case SwatchKind::Line:
            {
                const float lw = std::clamp(series_line_width(s), 1.0f, 4.0f);
                pc.line_width  = lw;
                pc.line_style  = static_cast<uint32_t>(st.line_style);
                if (st.line_style != LineStyle::Solid && st.line_style != LineStyle::None)
                {
                    // Shorter-than-usual dashes so a pattern repeat fits the swatch.
                    const DashPattern dp = get_dash_pattern(st.line_style, lw * 0.6f);
                    for (int k = 0; k < dp.count && k < 8; ++k)
                        pc.dash_pattern[k] = dp.segments[k];
                    pc.dash_total = dp.total;
                    pc.dash_count = dp.count;
                }
                const uint32_t seg = b.dash_aligned_segment(x, cy, x + SWATCH_W, cy, pc.dash_total);
                add(AnnotationDraw::Kind::Segment, seg, 2, pc);
                if (st.has_marker())
                {
                    SeriesPushConstants mpc{};
                    set_color(mpc, sc);
                    mpc.opacity     = 1.0f;
                    mpc.marker_type = static_cast<uint32_t>(st.marker_style);
                    mpc.point_size  = std::clamp(st.marker_size, 4.0f, 10.0f);
                    mpc.marker_size = mpc.point_size;
                    add(AnnotationDraw::Kind::Marker, b.point(x + SWATCH_W * 0.5f, cy), 1, mpc);
                }
                break;
            }
            case SwatchKind::Marker:
            {
                uint32_t marker = static_cast<uint32_t>(st.marker_style);
                float    size   = st.marker_size;
                if (const auto* scat = dynamic_cast<const ScatterSeries*>(&s))
                {
                    size = scat->size();
                    if (marker == 0)   // same default as render_series()
                        marker = static_cast<uint32_t>(bg_luma > 0.80f ? MarkerStyle::FilledCircle
                                                                       : MarkerStyle::Circle);
                }
                else if (const auto* s3 = dynamic_cast<const ScatterSeries3D*>(&s))
                {
                    size   = s3->size();   // same marker as the 3D scatter draw
                    marker = resolve_scatter_marker(s3->marker_style(), false, bg_luma).marker_type;
                }
                pc.marker_type = marker;
                pc.point_size  = std::clamp(size, 5.0f, 10.0f);
                pc.marker_size = pc.point_size;
                add(AnnotationDraw::Kind::Marker, b.point(x + SWATCH_W * 0.5f, cy), 1, pc);
                break;
            }
            case SwatchKind::Fill:
            {
                const float    sx    = x + (SWATCH_W - FILL_SWATCH) * 0.5f;
                const uint32_t first = b.vertex_count();
                b.rounded_fill(Rect{sx, cy - FILL_SWATCH * 0.5f, FILL_SWATCH, FILL_SWATCH}, 2.0f);
                add(AnnotationDraw::Kind::Triangles, first, b.vertex_count() - first, pc);
                break;
            }
        }

        Color text = text_base;
        text.a *= vis_alpha;
        const FittedText label =
            fit_text_to_width(s.label(), label_w, font, font, measure, ellipsis);
        text_renderer_.draw_text(label.text,
                                 x + SWATCH_W + SWATCH_GAP,
                                 cy,
                                 font,
                                 pack_rgba(text),
                                 TextAlign::Left,
                                 TextVAlign::Middle,
                                 font_scale);
    }

    // ── Overflow indicator: the last cell reports entries that did not fit ──
    if (grid.overflow > 0)
    {
        const int   row = grid.shown % grid.rows;
        const int   col = grid.shown / grid.rows;
        const float x  = box.x + pad_x + static_cast<float>(col) * (col_w + legend_layout::COL_GAP);
        const float cy = box.y + pad_y + static_cast<float>(row) * row_h + row_h * 0.5f;
        const std::string text = legend_layout::overflow_text(grid.overflow);
        text_renderer_.draw_text(text,
                                 x,
                                 cy,
                                 font,
                                 pack_rgba(text_base),
                                 TextAlign::Left,
                                 TextVAlign::Middle,
                                 font_scale);

        // Hover target: the whole cell (at least as wide as its text).
        LegendOverflowCell cell;
        cell.axes    = &axes;
        cell.cell    = Rect{x, cy - row_h * 0.5f, std::max(col_w, measure(text, font)), row_h};
        cell.hidden  = legend_layout::overflow_tooltip_lines(entries, grid.shown);
        cell.hovered = pointer_valid_ && pointer_x_ >= cell.cell.x
                       && pointer_x_ <= cell.cell.x + cell.cell.w && pointer_y_ >= cell.cell.y
                       && pointer_y_ <= cell.cell.y + cell.cell.h;
        if (cell.hovered)
            build_overflow_tooltip(cell, box, font, font_scale, row_h);
        legend_overflow_cells_.push_back(std::move(cell));
    }
}

// Tooltip card listing the entries a hovered "+N more" cell summarises.  It is
// placed beside the legend box (left, else right, else over it) so the legend
// text, which the text pass draws above all annotation geometry, does not
// show through it, and kept inside the figure.
void Renderer::build_overflow_tooltip(const LegendOverflowCell& cell,
                                      const Rect&               legend_box,
                                      FontSize                  font,
                                      float                     font_scale,
                                      float                     row_h)
{
    if (cell.hidden.empty())
        return;
    const auto& tc   = plot_colors();
    const char* ell  = text_renderer_.has_glyph(0x2026, font) ? nullptr : "...";
    auto        line = [&](const std::string& s)
    {
        // "\xE2\x80\xA6 and K more" falls back to ASCII without an ellipsis glyph.
        if (ell && s.rfind("\xE2\x80\xA6", 0) == 0)
            return std::string(ell) + s.substr(3);
        return s;
    };
    constexpr float PAD = 8.0f;
    constexpr float GAP = 6.0f;
    float           tw  = 0.0f;
    for (const auto& s : cell.hidden)
        tw = std::max(tw, text_renderer_.measure_text(line(s), font, font_scale).width);
    const float w  = std::min(tw + 2.0f * PAD, std::max(1.0f, annotation_fig_w_));
    const float h  = std::min(static_cast<float>(cell.hidden.size()) * row_h + 2.0f * PAD,
                             std::max(1.0f, annotation_fig_h_));
    const float fw = annotation_fig_w_;
    const float fh = annotation_fig_h_;

    float x = legend_box.x - GAP - w;   // left of the legend
    if (x < 0.0f)
        x = legend_box.x + legend_box.w + GAP;   // else right of it
    if (x + w > fw)
        x = std::max(0.0f, fw - w);   // else over it, inside the figure
    float y = std::clamp(cell.cell.y + cell.cell.h * 0.5f - h * 0.5f, 0.0f, std::max(0.0f, fh - h));
    const Rect card{x, y, w, h};

    AnnotationBuilder   b{annotation_vertex_scratch_, annotation_point_scratch_};
    SeriesPushConstants pc{};
    set_color(pc, theme_color(tc.tooltip_bg, theme_mgr_, 1.0f));
    uint32_t first = b.vertex_count();
    b.rounded_fill(card, CORNER_RADIUS);
    annotation_draws_.push_back(
        {AnnotationDraw::Kind::Triangles, first, b.vertex_count() - first, pc});
    set_color(pc, theme_color(tc.border_subtle, theme_mgr_));
    pc.line_width = 1.0f;
    first         = b.vertex_count();
    b.rounded_stroke(card, CORNER_RADIUS);
    annotation_draws_.push_back({AnnotationDraw::Kind::Lines, first, b.vertex_count() - first, pc});

    const uint32_t text_col = pack_rgba(theme_color(tc.text_primary, theme_mgr_));
    for (size_t i = 0; i < cell.hidden.size(); ++i)
    {
        const float cy = y + PAD + (static_cast<float>(i) + 0.5f) * row_h;
        if (cy > y + h)
            break;
        text_renderer_.draw_text(line(cell.hidden[i]),
                                 x + PAD,
                                 cy,
                                 font,
                                 text_col,
                                 TextAlign::Left,
                                 TextVAlign::Middle,
                                 font_scale);
    }
}

void Renderer::build_colorbar(AxesBase& axes)
{
    const auto source = colorbar_layout::find_source(axes);
    if (!source)
        return;

    const bool  is_3d = dynamic_cast<Axes3D*>(&axes) != nullptr;
    const Rect  bar   = colorbar_layout::bar_rect(axes.viewport(), is_3d);
    const auto& tc    = plot_colors();
    if (bar.h < 4.0f)
        return;

    AnnotationBuilder b{annotation_vertex_scratch_, annotation_point_scratch_};

    // ── Gradient: one solid band per ~2 px (each band its own push constant) ──
    const int   bands  = std::clamp(static_cast<int>(bar.h / 2.0f), 16, 256);
    const float band_h = bar.h / static_cast<float>(bands);
    for (int i = 0; i < bands; ++i)
    {
        // Band 0 is at the bottom (vmin); sample at the band centre.
        const float         t  = (static_cast<float>(i) + 0.5f) / static_cast<float>(bands);
        const float         y1 = bar.y + bar.h - band_h * static_cast<float>(i);
        const float         y0 = (i == bands - 1) ? bar.y : y1 - band_h;
        SeriesPushConstants pc{};
        set_color(pc, colorbar_layout::sample(source->cmap, source->mapping, t));
        const uint32_t first = b.vertex_count();
        b.quad(bar.x, y0, bar.x + bar.w, y1);
        annotation_draws_.push_back({AnnotationDraw::Kind::Triangles, first, 6, pc});
    }

    // ── Outline + tick marks + tick labels ──
    const auto     ticks = colorbar_layout::compute_ticks(source->vmin, source->vmax);
    const uint32_t first = b.vertex_count();
    b.line(bar.x, bar.y, bar.x + bar.w, bar.y);
    b.line(bar.x + bar.w, bar.y, bar.x + bar.w, bar.y + bar.h);
    b.line(bar.x + bar.w, bar.y + bar.h, bar.x, bar.y + bar.h);
    b.line(bar.x, bar.y + bar.h, bar.x, bar.y);
    const double   range    = source->vmax - source->vmin;
    const uint32_t tick_col = pack_rgba(theme_color(tc.tick_label, theme_mgr_));
    for (const auto& tick : ticks)
    {
        const double t = range != 0.0 ? (tick.value - source->vmin) / range : 0.5;
        const float  y = bar.y + static_cast<float>(1.0 - std::clamp(t, 0.0, 1.0)) * bar.h;
        const float  x = bar.x + bar.w;
        b.line(x, y, x + colorbar_layout::TICK_LENGTH, y);
        if (text_renderer_.is_initialized())
            text_renderer_.draw_text(tick.label,
                                     x + colorbar_layout::TICK_LENGTH + colorbar_layout::LABEL_GAP,
                                     y,
                                     FontSize::Tick,
                                     tick_col,
                                     TextAlign::Left,
                                     TextVAlign::Middle);
    }
    SeriesPushConstants pc{};
    set_color(pc, theme_color(tc.axis_line, theme_mgr_));
    pc.line_width = 1.0f;
    annotation_draws_.push_back({AnnotationDraw::Kind::Lines, first, b.vertex_count() - first, pc});
}

}   // namespace spectra
