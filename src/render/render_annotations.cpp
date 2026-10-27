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
#include <spectra/series_stats.hpp>

#include "core/colorbar_layout.hpp"
#include "renderer.hpp"
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

bool is_unset(const Color& c)
{
    // LegendConfig uses {} (black) as "use the theme colour"; see figure.hpp.
    return c.r == 0.0f && c.g == 0.0f && c.b == 0.0f;
}

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

// LegendConfig::font_size → nearest baked TextRenderer size (0 = default).
FontSize legend_font(float font_size)
{
    if (font_size <= 0.0f)
        return FontSize::Label;
    if (font_size < 12.5f)
        return FontSize::Tick;
    if (font_size < 17.0f)
        return FontSize::Label;
    return FontSize::Title;
}

enum class SwatchKind
{
    Line,     // short line in the series' line style (+ marker if it has one)
    Marker,   // a single marker
    Fill,     // filled square (bars, histograms, bands, box/violin, shapes)
};

SwatchKind swatch_kind(const Series& s)
{
    if (dynamic_cast<const ScatterSeries*>(&s))
        return SwatchKind::Marker;
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
    return 2.0f;
}

}   // namespace

void Renderer::render_annotations(Figure& figure)
{
    annotation_draws_.clear();
    annotation_vertex_scratch_.clear();
    annotation_point_scratch_.clear();

    const float fh = static_cast<float>(figure.height());

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

void Renderer::build_legend(Axes& axes, const LegendConfig& config)
{
    if (!text_renderer_.is_initialized())
        return;

    std::vector<const Series*> entries;
    for (const auto& s : axes.series())
    {
        if (s && !s->label().empty() && s->show_in_legend())
            entries.push_back(s.get());
    }
    if (entries.empty())
        return;

    const auto&    tc   = theme_mgr_.colors();
    const Rect&    vp   = axes.viewport();
    const FontSize font = legend_font(config.font_size);

    float max_label_w = 0.0f;
    float text_h      = 0.0f;
    for (const Series* s : entries)
    {
        const auto ext = text_renderer_.measure_text(s->label(), font);
        max_label_w    = std::max(max_label_w, ext.width);
        text_h         = std::max(text_h, ext.height);
    }

    const float pad_x = config.padding + 2.0f;   // as LegendInteraction
    const float pad_y = config.padding;
    const float row_h = text_h + 6.0f;
    const float col_w = SWATCH_W + SWATCH_GAP + max_label_w;
    const auto  grid  = legend_layout::compute_grid(static_cast<int>(entries.size()),
                                                  col_w,
                                                  row_h,
                                                  pad_x,
                                                  pad_y,
                                                  legend_layout::COL_GAP,
                                                  vp.w - 2.0f * legend_layout::MARGIN,
                                                  vp.h - 2.0f * legend_layout::MARGIN);
    if (grid.rows <= 0)
        return;

    auto                 it   = legend_best_corner_.find(&axes);
    const LegendPosition prev = it != legend_best_corner_.end() ? it->second : LegendPosition::None;
    const LegendPosition corner =
        legend_layout::resolve_position(axes, vp, grid.width, grid.height, config.position, prev);
    if (config.position == LegendPosition::Best)
        legend_best_corner_[&axes] = corner;
    const Rect box = legend_layout::place(vp, corner, grid.width, grid.height);

    AnnotationBuilder b{annotation_vertex_scratch_, annotation_point_scratch_};
    auto              add = [this](AnnotationDraw::Kind       k,
                      uint32_t                   first,
                      uint32_t                   count,
                      const SeriesPushConstants& pc)
    { annotation_draws_.push_back({k, first, count, pc}); };

    // ── Box: background + border ──
    {
        const Color bg = is_unset(config.bg_color) ? theme_color(tc.tooltip_bg, theme_mgr_, 0.9f)
                                                   : config.bg_color;
        const Color border = is_unset(config.border_color)
                                 ? theme_color(tc.border_subtle, theme_mgr_)
                                 : config.border_color;
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
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const int row = static_cast<int>(i) % grid.rows;
        const int col = static_cast<int>(i) / grid.rows;
        if (row >= grid.visible_rows)
            continue;   // cannot scroll in a static image
        const Series& s = *entries[i];
        const float x  = box.x + pad_x + static_cast<float>(col) * (col_w + legend_layout::COL_GAP);
        const float y  = box.y + pad_y + static_cast<float>(row) * row_h;
        const float cy = y + row_h * 0.5f;

        const float vis_alpha = s.visible() ? 1.0f : HIDDEN_ALPHA;
        Color       sc        = ui::adapt_series_color_to_theme(s.color(), tc);
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
        text_renderer_.draw_text(s.label(),
                                 x + SWATCH_W + SWATCH_GAP,
                                 cy,
                                 font,
                                 pack_rgba(text),
                                 TextAlign::Left,
                                 TextVAlign::Middle);
    }
}

void Renderer::build_colorbar(AxesBase& axes)
{
    const auto source = colorbar_layout::find_source(axes);
    if (!source)
        return;

    const bool  is_3d = dynamic_cast<Axes3D*>(&axes) != nullptr;
    const Rect  bar   = colorbar_layout::bar_rect(axes.viewport(), is_3d);
    const auto& tc    = theme_mgr_.colors();
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
