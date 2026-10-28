// Legend and colorbar drawn by the renderer (headless / export path):
//   - legend_layout placement helpers shared with the ImGui legend
//   - colorbar layout: space reservation, source detection, ticks, colours
//   - Renderer issues legend/colorbar draws, and skips the legend while the
//     interactive ImGui legend overlay is active.

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_shapes.hpp>
#include <spectra/series_stats.hpp>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/colorbar_layout.hpp"
#include "render/backend.hpp"
#include "render/renderer.hpp"
#include "ui/overlay/legend_layout.hpp"
#include "ui/theme/theme.hpp"

using namespace spectra;

namespace
{

// CPU-only backend recording every draw with its pipeline type and push
// constants.
class RecordingBackend : public Backend
{
   public:
    struct Draw
    {
        PipelineType        pipeline;
        SeriesPushConstants pc;
        uint32_t            count;
    };

    bool init(bool) override { return true; }
    void shutdown() override {}
    void wait_idle() override {}
    bool create_surface(void*) override { return true; }
    bool create_swapchain(uint32_t, uint32_t) override { return true; }
    bool recreate_swapchain(uint32_t, uint32_t) override { return true; }
    bool create_offscreen_framebuffer(uint32_t, uint32_t) override { return true; }

    PipelineHandle create_pipeline(PipelineType type) override
    {
        PipelineHandle h{next_id_++};
        pipeline_types_[h.id] = type;
        return h;
    }
    BufferHandle  create_buffer(BufferUsage, size_t) override { return BufferHandle{next_id_++}; }
    void          destroy_buffer(BufferHandle) override {}
    void          upload_buffer(BufferHandle, const void*, size_t, size_t) override {}
    TextureHandle create_texture(uint32_t, uint32_t, const uint8_t*) override
    {
        return TextureHandle{next_id_++};
    }
    void destroy_texture(TextureHandle) override {}

    bool begin_frame(FrameProfiler*) override { return true; }
    void end_frame(FrameProfiler*) override {}
    void begin_render_pass(const Color&) override {}
    void end_render_pass() override {}
    void bind_pipeline(PipelineHandle h) override
    {
        auto it = pipeline_types_.find(h.id);
        bound_  = it != pipeline_types_.end() ? it->second : PipelineType::Image3D;
    }
    void bind_buffer(BufferHandle, uint32_t) override {}
    void bind_index_buffer(BufferHandle) override {}
    void bind_texture(TextureHandle, uint32_t) override {}
    void push_constants(const SeriesPushConstants& pc) override { pc_ = pc; }
    void set_viewport(float, float, float, float) override {}
    void set_scissor(int32_t, int32_t, uint32_t, uint32_t) override {}
    void set_line_width(float) override {}
    void draw(uint32_t n, uint32_t) override { draws_.push_back({bound_, pc_, n}); }
    void draw_instanced(uint32_t n, uint32_t, uint32_t, uint32_t) override
    {
        draws_.push_back({bound_, pc_, n});
    }
    void draw_indexed(uint32_t n, uint32_t, int32_t) override
    {
        draws_.push_back({bound_, pc_, n});
    }
    bool     readback_framebuffer(uint8_t*, uint32_t, uint32_t) override { return false; }
    uint32_t swapchain_width() const override { return 800; }
    uint32_t swapchain_height() const override { return 600; }

    size_t count(PipelineType t) const
    {
        return static_cast<size_t>(std::count_if(draws_.begin(),
                                                 draws_.end(),
                                                 [t](const Draw& d) { return d.pipeline == t; }));
    }
    bool has_color(PipelineType t, const Color& c, float tol = 1e-3f) const
    {
        return std::any_of(draws_.begin(),
                           draws_.end(),
                           [&](const Draw& d)
                           {
                               return d.pipeline == t && std::abs(d.pc.color[0] - c.r) < tol
                                      && std::abs(d.pc.color[1] - c.g) < tol
                                      && std::abs(d.pc.color[2] - c.b) < tol;
                           });
    }

    std::vector<Draw> draws_;

   private:
    uint64_t                                   next_id_ = 1;
    std::unordered_map<uint64_t, PipelineType> pipeline_types_;
    PipelineType                               bound_ = PipelineType::Image3D;
    SeriesPushConstants                        pc_{};
};

std::vector<float> ramp(int n, float a, float b)
{
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        v[static_cast<size_t>(i)] = a + (b - a) * static_cast<float>(i) / static_cast<float>(n - 1);
    return v;
}

}   // namespace

// ─── legend_layout placement ────────────────────────────────────────────────

TEST(LegendLayoutPlace, CornersHonourMarginAndClamp)
{
    const Rect  vp{100.0f, 50.0f, 400.0f, 300.0f};
    const float m = legend_layout::MARGIN;

    Rect tr = legend_layout::place(vp, LegendPosition::TopRight, 80.0f, 40.0f);
    EXPECT_FLOAT_EQ(tr.x, vp.x + vp.w - 80.0f - m);
    EXPECT_FLOAT_EQ(tr.y, vp.y + m);

    Rect bl = legend_layout::place(vp, LegendPosition::BottomLeft, 80.0f, 40.0f);
    EXPECT_FLOAT_EQ(bl.x, vp.x + m);
    EXPECT_FLOAT_EQ(bl.y, vp.y + vp.h - 40.0f - m);

    // A drag offset pushing the box outside is clamped back inside the viewport.
    Rect dragged = legend_layout::place(vp, LegendPosition::TopLeft, 80.0f, 40.0f, -500.0f, 900.0f);
    EXPECT_FLOAT_EQ(dragged.x, vp.x + legend_layout::CLAMP_INSET);
    EXPECT_FLOAT_EQ(dragged.y, vp.y + vp.h - 40.0f - legend_layout::CLAMP_INSET);
}

TEST(LegendLayoutPlace, ResolveBestAvoidsData)
{
    Axes ax;
    // Data hugging the top-right: y high where x is high.
    auto               x = ramp(200, 0.0f, 10.0f);
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = x[i] > 7.0f ? 9.0f : 5.0f;
    ax.line(x, y);
    ax.xlim(0.0, 10.0);
    ax.ylim(0.0, 10.0);
    const Rect vp{0.0f, 0.0f, 400.0f, 300.0f};

    EXPECT_EQ(legend_layout::resolve_position(ax, vp, 80, 40, LegendPosition::BottomLeft),
              LegendPosition::BottomLeft);
    EXPECT_NE(legend_layout::resolve_position(ax, vp, 80, 40, LegendPosition::Best),
              LegendPosition::TopRight);
}

// Audit S02: the shared legend helpers accept 3D axes.  "Best" cannot score
// a camera-dependent projection, so it resolves to TopRight (as in SVG).
TEST(LegendLayoutPlace, Axes3DEntriesPositionAndSwatchColour)
{
    Axes3D ax;
    auto   x = ramp(10, 0.0f, 1.0f);
    ax.line3d(x, x, x).label("line");
    ax.scatter3d(x, x, x);   // unlabeled: no entry
    ax.scatter3d(x, x, x).label("hidden from legend").set_show_in_legend(false);
    auto&              hidden = ax.scatter3d(x, x, x).label("hidden");
    std::vector<float> g      = ramp(4, -1.0f, 1.0f), z(16, 0.5f);
    SurfaceSeries&     surf   = ax.surface(g, g, z);
    surf.label("surf");
    hidden.visible(false);

    const auto entries = legend_layout::legend_entries(ax);
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0]->label(), "line");
    EXPECT_EQ(entries[1]->label(), "hidden");   // listed (dimmed) so it can be re-enabled
    EXPECT_EQ(entries[2]->label(), "surf");

    const Rect vp{0.0f, 0.0f, 400.0f, 300.0f};
    EXPECT_EQ(legend_layout::resolve_position(ax, vp, 80, 40, LegendPosition::Best),
              LegendPosition::TopRight);
    EXPECT_EQ(legend_layout::resolve_position(ax, vp, 80, 40, LegendPosition::BottomLeft),
              LegendPosition::BottomLeft);

    surf.color(Color{0.1f, 0.2f, 0.3f, 1.0f});
    const Color plain = legend_layout::swatch_color(surf);
    EXPECT_FLOAT_EQ(plain.g, 0.2f);
    surf.colormap(ColormapType::Jet);
    const Color mapped = legend_layout::swatch_color(surf);
    const Color mid    = SurfaceSeries::sample_colormap(ColormapType::Jet, 0.5f);
    EXPECT_FLOAT_EQ(mapped.r, mid.r);
    EXPECT_FLOAT_EQ(mapped.g, mid.g);
    EXPECT_FLOAT_EQ(mapped.b, mid.b);
}

// ─── "Best" placement against filled areas ─────────────────────────────────
// Filled series (bars, histograms, boxes, violins, bands, shapes) are scored
// by the area they cover, not only by their outline vertices, so a legend is
// not placed on top of a bar or violin whose outline lies outside the box.

TEST(LegendLayoutBestFilled, TriangleRectOverlapArea)
{
    const Rect r{0.0f, 0.0f, 10.0f, 10.0f};
    // Fully inside: area of the triangle itself.
    EXPECT_NEAR(legend_layout::detail::triangle_rect_overlap({1, 1, 5, 1, 1, 5}, r), 8.0f, 1e-4f);
    // Fully covering the rect: area of the rect.
    EXPECT_NEAR(legend_layout::detail::triangle_rect_overlap({-50, -50, 100, -50, -50, 100}, r),
                100.0f,
                1e-3f);
    // Half the rect (diagonal split).
    EXPECT_NEAR(legend_layout::detail::triangle_rect_overlap({0, 0, 10, 0, 0, 10}, r),
                50.0f,
                1e-4f);
    // Disjoint.
    EXPECT_NEAR(legend_layout::detail::triangle_rect_overlap({20, 20, 30, 20, 20, 30}, r),
                0.0f,
                1e-6f);
}

namespace
{
const Rect  kVp{0.0f, 0.0f, 400.0f, 300.0f};
const float kW = 90.0f;
const float kH = 32.0f;

size_t score(const Axes& ax, LegendPosition pos)
{
    return legend_layout::overlap_score(ax, kVp, pos, kW, kH, legend_layout::MARGIN);
}

// A wide filled block covering the top-right corner whose outline vertices
// all lie far outside the legend box: only area scoring can see it.
void expect_avoids_top_right(const Axes& ax)
{
    EXPECT_GE(score(ax, LegendPosition::TopRight),
              static_cast<size_t>(legend_layout::FILL_OVERLAP_WEIGHT * 0.9f));
    const LegendPosition best =
        legend_layout::choose_best_corner(ax, kVp, kW, kH, legend_layout::MARGIN);
    EXPECT_NE(best, LegendPosition::TopRight);
    EXPECT_EQ(score(ax, best), 0u);
    // Even when the legend currently sits there (hysteresis), it moves off.
    EXPECT_NE(legend_layout::choose_best_corner(ax,
                                                kVp,
                                                kW,
                                                kH,
                                                legend_layout::MARGIN,
                                                LegendPosition::TopRight),
              LegendPosition::TopRight);
}
}   // namespace

TEST(LegendLayoutBestFilled, AvoidsWideBar)
{
    Axes               ax;
    std::vector<float> pos = {7.5f};
    std::vector<float> h   = {100.0f};
    auto&              bar = ax.bar(pos, h);
    bar.bar_width(5.0f);
    bar.rebuild_geometry();
    ax.xlim(0.0, 10.0);
    ax.ylim(0.0, 100.0);
    expect_avoids_top_right(ax);
}

TEST(LegendLayoutBestFilled, AvoidsBand)
{
    Axes               ax;
    std::vector<float> x  = {5.0f, 10.0f};
    std::vector<float> lo = {50.0f, 50.0f};
    std::vector<float> hi = {100.0f, 100.0f};
    ax.band(x, lo, hi);
    ax.xlim(0.0, 10.0);
    ax.ylim(0.0, 100.0);
    expect_avoids_top_right(ax);
}

TEST(LegendLayoutBestFilled, AvoidsShape)
{
    Axes  ax;
    auto& sh = ax.shapes();
    sh.rect(5.0f, 50.0f, 5.0f, 50.0f);
    sh.rebuild_geometry();
    ax.xlim(0.0, 10.0);
    ax.ylim(0.0, 100.0);
    expect_avoids_top_right(ax);
}

TEST(LegendLayoutBestFilled, AvoidsBoxBody)
{
    Axes               ax;
    std::vector<float> v  = ramp(101, 0.0f, 100.0f);
    auto&              bp = ax.box_plot();
    bp.box_width(5.0f).add_box(7.5f, v);
    bp.rebuild_geometry();
    ax.xlim(0.0, 10.0);
    ax.ylim(35.0, 65.0);   // zoomed into the box body: its edges are off-screen
    expect_avoids_top_right(ax);
}

TEST(LegendLayoutBestFilled, AvoidsViolinBody)
{
    Axes               ax;
    std::vector<float> v  = ramp(201, 0.0f, 100.0f);
    auto&              vn = ax.violin();
    vn.violin_width(5.0f).add_violin(7.5f, v);
    vn.rebuild_geometry();
    ax.xlim(0.0, 10.0);
    ax.ylim(40.0, 60.0);   // zoomed into the violin body
    expect_avoids_top_right(ax);
}

TEST(LegendLayoutBestFilled, AvoidsHistogramBins)
{
    Axes ax;
    // All samples in the upper half of the range: tall bins on the right.
    std::vector<float> v;
    for (int i = 0; i < 400; ++i)
        v.push_back(i < 4 ? 0.0f : 6.0f + 4.0f * static_cast<float>(i % 100) / 100.0f);
    auto& hs = ax.histogram(v, 10);
    hs.rebuild_geometry();
    ax.xlim(0.0, 10.0);
    ax.ylim(0.0, 30.0);
    EXPECT_GT(score(ax, LegendPosition::TopRight), score(ax, LegendPosition::TopLeft));
    EXPECT_NE(legend_layout::choose_best_corner(ax, kVp, kW, kH, legend_layout::MARGIN),
              LegendPosition::TopRight);
}

TEST(LegendLayoutBestFilled, HiddenFillIgnored)
{
    Axes               ax;
    std::vector<float> pos = {7.5f};
    std::vector<float> h   = {100.0f};
    auto&              bar = ax.bar(pos, h);
    bar.bar_width(5.0f);
    bar.rebuild_geometry();
    bar.visible(false);
    ax.xlim(0.0, 10.0);
    ax.ylim(0.0, 100.0);
    EXPECT_EQ(score(ax, LegendPosition::TopRight), 0u);
    EXPECT_EQ(legend_layout::choose_best_corner(ax, kVp, kW, kH, legend_layout::MARGIN),
              LegendPosition::TopRight);
}

TEST(LegendLayoutBestFilled, DataGrazingTheBoxEdgeCounts)
{
    // A point a few pixels left of the top-right legend box still crowds it.
    Axes ax;
    ax.xlim(0.0, 400.0);
    ax.ylim(0.0, 300.0);
    const float        box_left = kVp.w - legend_layout::MARGIN - kW;
    std::vector<float> x        = {box_left - 0.5f * legend_layout::CLEARANCE};
    std::vector<float> y        = {300.0f - legend_layout::MARGIN - 0.5f * kH};
    ax.scatter(x, y);
    EXPECT_GT(score(ax, LegendPosition::TopRight), 0u);
    EXPECT_NE(legend_layout::choose_best_corner(ax, kVp, kW, kH, legend_layout::MARGIN),
              LegendPosition::TopRight);
}

// ─── colorbar layout ────────────────────────────────────────────────────────

TEST(ColorbarLayout, ReservesSpaceOnlyForColormappedSeriesWithFlag)
{
    const Rect cell{10.0f, 20.0f, 600.0f, 400.0f};
    Axes       ax;
    auto       x = ramp(10, 0.0f, 1.0f);
    ax.scatter(x, x);
    ax.show_colorbar(true);
    ax.set_viewport(cell);
    EXPECT_FLOAT_EQ(ax.viewport().w, cell.w) << "no colormap: nothing reserved";

    Axes cm_ax;
    cm_ax.scatter(x, x).color_values(x).colormap(ColormapType::Viridis);
    cm_ax.set_viewport(cell);
    EXPECT_FLOAT_EQ(cm_ax.viewport().w, cell.w) << "colorbar not requested";

    cm_ax.show_colorbar(true);
    cm_ax.set_viewport(cell);
    EXPECT_FLOAT_EQ(cm_ax.viewport().x, cell.x);
    EXPECT_FLOAT_EQ(cm_ax.viewport().h, cell.h);
    EXPECT_FLOAT_EQ(cm_ax.viewport().w, cell.w - colorbar_layout::RESERVED_WIDTH);

    // Idempotent: re-applying the same cell does not shrink again.
    cm_ax.set_viewport(cell);
    EXPECT_FLOAT_EQ(cm_ax.viewport().w, cell.w - colorbar_layout::RESERVED_WIDTH);

    // The bar and its labels fit in the reserved strip.
    const Rect bar = colorbar_layout::bar_rect(cm_ax.viewport(), false);
    EXPECT_GT(bar.x, cm_ax.viewport().x + cm_ax.viewport().w);
    EXPECT_LE(bar.x + bar.w + colorbar_layout::TICK_LENGTH + colorbar_layout::LABEL_GAP
                  + colorbar_layout::LABEL_WIDTH,
              cell.x + cell.w + 0.01f);
}

TEST(ColorbarLayout, NarrowCellsKeepMostOfThePlot)
{
    EXPECT_FLOAT_EQ(colorbar_layout::reserved_width(100.0f), 30.0f);
    EXPECT_FLOAT_EQ(colorbar_layout::reserved_width(2000.0f), colorbar_layout::RESERVED_WIDTH);
}

TEST(ColorbarLayout, SourceFromScatterAndSurface)
{
    Axes ax;
    auto x = ramp(10, 0.0f, 1.0f);
    auto c = ramp(10, -2.0f, 6.0f);
    ax.scatter(x, x).color_values(c).colormap(ColormapType::Plasma);
    auto src = colorbar_layout::find_source(ax);
    ASSERT_TRUE(src.has_value());
    EXPECT_EQ(src->cmap, ColormapType::Plasma);
    EXPECT_EQ(src->mapping, colorbar_layout::Mapping::Scatter);
    EXPECT_DOUBLE_EQ(src->vmin, -2.0);
    EXPECT_DOUBLE_EQ(src->vmax, 6.0);

    Axes3D             ax3;
    std::vector<float> gx = ramp(4, -1.0f, 1.0f), gy = ramp(4, -1.0f, 1.0f), gz(16, 0.0f);
    for (size_t i = 0; i < gz.size(); ++i)
        gz[i] = static_cast<float>(i) * 0.1f;
    ax3.surface(gx, gy, gz).colormap(ColormapType::Viridis);
    ax3.zlim(-1.0, 3.0);
    auto s3 = colorbar_layout::find_source(ax3);
    ASSERT_TRUE(s3.has_value());
    EXPECT_EQ(s3->mapping, colorbar_layout::Mapping::Surface);
    EXPECT_DOUBLE_EQ(s3->vmin, -1.0);   // surface3d.frag maps the z limits
    EXPECT_DOUBLE_EQ(s3->vmax, 3.0);
}

TEST(ColorbarLayout, ScatterSamplingMatchesShaderStops)
{
    using colorbar_layout::Mapping;
    const Color lo  = colorbar_layout::sample(ColormapType::Viridis, Mapping::Scatter, 0.0f);
    const Color mid = colorbar_layout::sample(ColormapType::Viridis, Mapping::Scatter, 0.5f);
    const Color hi  = colorbar_layout::sample(ColormapType::Viridis, Mapping::Scatter, 1.0f);
    EXPECT_NEAR(lo.r, 0.267f, 1e-4f);
    EXPECT_NEAR(mid.g, 0.567f, 1e-4f);
    EXPECT_NEAR(hi.b, 0.144f, 1e-4f);
    const Color g = colorbar_layout::sample(ColormapType::Grayscale, Mapping::Scatter, 0.25f);
    EXPECT_FLOAT_EQ(g.r, 0.25f);
    // Surface mapping is the series' own sampler.
    const Color s = colorbar_layout::sample(ColormapType::Jet, Mapping::Surface, 0.3f);
    const Color e = SurfaceSeries::sample_colormap(ColormapType::Jet, 0.3f);
    EXPECT_FLOAT_EQ(s.r, e.r);
    EXPECT_FLOAT_EQ(s.g, e.g);
}

TEST(ColorbarLayout, TicksAreNiceAndInRange)
{
    struct Case
    {
        double lo, hi;
    };
    for (Case c : {Case{0.0, 1.0},
                   Case{-1.0, 1.0},
                   Case{0.0, 100.0},
                   Case{0.05, 3.9},
                   Case{-3.0, 3.0},
                   Case{1e-3, 7e-3},
                   Case{250.0, 1300.0}})
    {
        const auto ticks = colorbar_layout::compute_ticks(c.lo, c.hi);
        EXPECT_GE(ticks.size(), 4u) << c.lo << ".." << c.hi;
        EXPECT_LE(ticks.size(), 7u) << c.lo << ".." << c.hi;
        for (size_t i = 0; i < ticks.size(); ++i)
        {
            EXPECT_GE(ticks[i].value, c.lo - 1e-9);
            EXPECT_LE(ticks[i].value, c.hi + 1e-9);
            if (i > 0)
            {
                EXPECT_GT(ticks[i].value, ticks[i - 1].value);
            }
            EXPECT_FALSE(ticks[i].label.empty());
        }
    }
    const auto q = colorbar_layout::compute_ticks(0.0, 1.0);
    ASSERT_EQ(q.size(), 5u);
    EXPECT_EQ(q[1].label, "0.25");
    EXPECT_EQ(q[4].label, "1.00");

    const auto one = colorbar_layout::compute_ticks(2.0, 2.0);
    ASSERT_EQ(one.size(), 1u);
    EXPECT_TRUE(colorbar_layout::compute_ticks(NAN, 1.0).empty());
}

// ─── Renderer ───────────────────────────────────────────────────────────────

namespace
{

struct RendererFixture : ::testing::Test
{
    RecordingBackend backend;
    ui::ThemeManager theme;
    Renderer         renderer{backend, theme};

    void SetUp() override { ASSERT_TRUE(renderer.init()); }

    void render(Figure& fig)
    {
        fig.compute_layout();
        for (auto& ax : fig.axes_mut())
            for (auto& s : ax->series_mut())
                renderer.upload_series_data(*s);
        for (auto& ax : fig.all_axes_mut())
            for (auto& s : ax->series_mut())
                renderer.upload_series_data(*s);
        backend.draws_.clear();
        renderer.begin_render_pass();
        renderer.render_figure_content(fig);
        renderer.end_render_pass();
    }
};

}   // namespace

TEST_F(RendererFixture, LegendDrawnWithSwatchesUnlessOverlayActive)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = fig.subplot(1, 1, 1);
    auto   x  = ramp(20, 0.0f, 1.0f);
    ax.plot(x, x, "r--").label("dashed");
    ax.plot(x, x, "g-o").label("with markers");
    ax.bar(x, x).label("bars");

    const Color legend_bg = [&]
    {
        const auto& c = theme.colors().tooltip_bg;
        return Color{c.r, c.g, c.b, c.a};
    }();

    render(fig);
    const size_t overlay_draws = backend.count(PipelineType::Overlay);
    const size_t line_draws    = backend.count(PipelineType::Line);
    const size_t marker_draws  = backend.count(PipelineType::Scatter);
    EXPECT_TRUE(backend.has_color(PipelineType::Overlay, legend_bg)) << "legend background";
    // 2 line swatches + 1 marker on top of the series' own draws.
    EXPECT_GE(line_draws, 4u);
    EXPECT_GE(marker_draws, 2u);
    // Dashed swatch carries the dash pattern.
    EXPECT_TRUE(std::any_of(backend.draws_.begin(),
                            backend.draws_.end(),
                            [](const RecordingBackend::Draw& d) {
                                return d.pipeline == PipelineType::Line && d.count == 6
                                       && d.pc.dash_count > 0;
                            }));

    renderer.set_legend_overlay_active(true);
    render(fig);
    EXPECT_FALSE(backend.has_color(PipelineType::Overlay, legend_bg));
    EXPECT_LT(backend.count(PipelineType::Overlay), overlay_draws);
    EXPECT_EQ(backend.count(PipelineType::Line), line_draws - 2);
    EXPECT_EQ(backend.count(PipelineType::Scatter), marker_draws - 1);

    // render_figure() (offscreen export) always draws the legend itself and
    // leaves the overlay setting alone.
    backend.draws_.clear();
    fig.compute_layout();
    renderer.render_figure(fig);
    EXPECT_TRUE(backend.has_color(PipelineType::Overlay, legend_bg));
    EXPECT_TRUE(renderer.legend_overlay_active());
}

TEST_F(RendererFixture, LegendHiddenByConfig)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = fig.subplot(1, 1, 1);
    auto   x  = ramp(20, 0.0f, 1.0f);
    ax.plot(x, x).label("a");
    const auto& c = theme.colors().tooltip_bg;
    const Color bg{c.r, c.g, c.b, c.a};

    fig.legend().visible = false;
    render(fig);
    EXPECT_FALSE(backend.has_color(PipelineType::Overlay, bg));

    fig.legend().visible  = true;
    fig.legend().position = LegendPosition::None;
    render(fig);
    EXPECT_FALSE(backend.has_color(PipelineType::Overlay, bg));

    // A user background colour replaces the theme one.
    fig.legend().position = LegendPosition::TopLeft;
    fig.legend().bg_color = Color{0.2f, 0.4f, 0.6f, 1.0f};
    render(fig);
    EXPECT_TRUE(backend.has_color(PipelineType::Overlay, fig.legend().bg_color));
}

TEST_F(RendererFixture, ColorbarDrawnForColormappedScatterAndSurface)
{
    Figure fig({.width = 900, .height = 600});
    auto&  ax = fig.subplot(1, 2, 1);
    auto   x  = ramp(50, 0.0f, 1.0f);
    ax.scatter(x, x).color_values(x).colormap(ColormapType::Viridis);

    const Color top =
        colorbar_layout::sample(ColormapType::Viridis, colorbar_layout::Mapping::Scatter, 0.99f);

    render(fig);
    EXPECT_FALSE(backend.has_color(PipelineType::Overlay, top, 0.02f)) << "flag off: no colorbar";

    ax.show_colorbar(true);
    render(fig);
    EXPECT_GE(backend.count(PipelineType::Overlay), 16u);
    EXPECT_TRUE(backend.has_color(PipelineType::Overlay, top, 0.02f));

    // 3D surface
    Figure             fig3({.width = 900, .height = 600});
    auto&              ax3 = fig3.subplot3d(1, 1, 1);
    std::vector<float> g   = ramp(8, -1.0f, 1.0f), z(64);
    for (size_t i = 0; i < z.size(); ++i)
        z[i] = static_cast<float>(i) / 64.0f;
    ax3.surface(g, g, z).colormap(ColormapType::Jet);
    ax3.show_colorbar(true);
    const Color jet_top = SurfaceSeries::sample_colormap(ColormapType::Jet, 0.99f);
    render(fig3);
    EXPECT_TRUE(backend.has_color(PipelineType::Overlay, jet_top, 0.03f));
}

// Audit S03: a static export cannot scroll, so a legend with more entries than
// fit must not silently drop rows.  The drawn swatches are the first entries
// in order (no gaps), and the rest are summarised by a "+N more" cell.
TEST_F(RendererFixture, StaticLegendOverflowKeepsEntriesContiguous)
{
    Figure             fig({.width = 800, .height = 600});
    auto&              ax = fig.subplot(3, 1, 1);   // wide but short panel
    auto               x  = ramp(20, 0.0f, 1.0f);
    constexpr int      N  = 40;
    std::vector<Color> colors;
    for (int i = 0; i < N; ++i)
    {
        // Distinct, theme-independent colours so swatches map back to series.
        const float t = static_cast<float>(i) / static_cast<float>(N);
        const Color c{0.2f + 0.6f * t, 0.5f, 0.8f - 0.6f * t, 1.0f};
        colors.push_back(c);
        auto& s = ax.plot(x, x, "-").label("Series " + std::to_string(i + 1));
        s.color(c);
    }

    const auto& tc = theme.colors();
    const Color legend_bg{tc.tooltip_bg.r, tc.tooltip_bg.g, tc.tooltip_bg.b, tc.tooltip_bg.a};

    render(fig);
    // Legend swatches are 2-point segments (6 vertices) drawn after the
    // legend background; earlier draws are the series themselves.
    std::vector<int> drawn;
    bool             in_legend = false;
    for (const auto& d : backend.draws_)
    {
        if (d.pipeline == PipelineType::Overlay && std::abs(d.pc.color[0] - legend_bg.r) < 1e-3f
            && std::abs(d.pc.color[1] - legend_bg.g) < 1e-3f
            && std::abs(d.pc.color[2] - legend_bg.b) < 1e-3f)
            in_legend = true;
        if (!in_legend || d.pipeline != PipelineType::Line || d.count != 6)
            continue;
        for (int i = 0; i < N; ++i)
        {
            const Color a = ui::adapt_series_color_to_theme(colors[static_cast<size_t>(i)], tc);
            if (std::abs(d.pc.color[0] - a.r) < 1e-4f && std::abs(d.pc.color[2] - a.b) < 1e-4f)
            {
                drawn.push_back(i);
                break;
            }
        }
    }
    ASSERT_FALSE(drawn.empty());
    ASSERT_LT(drawn.size(), static_cast<size_t>(N)) << "panel is too small for all 40";
    for (size_t k = 0; k < drawn.size(); ++k)
        EXPECT_EQ(drawn[k], static_cast<int>(k)) << "legend entries must be contiguous";
}

// Round-2 follow-up: "+N more" names the hidden entries on hover.
TEST(LegendLayoutOverflow, TooltipListsHiddenEntriesInOrder)
{
    std::vector<std::string> labels;
    for (int i = 0; i < 30; ++i)
        labels.push_back("S" + std::to_string(i));
    auto lines = legend_layout::overflow_tooltip_lines(std::span<const std::string>(labels), 25);
    EXPECT_EQ(lines, (std::vector<std::string>{"S25", "S26", "S27", "S28", "S29"}));
    // Capped: 20 names, then a count of the rest.
    lines = legend_layout::overflow_tooltip_lines(std::span<const std::string>(labels), 2, 20);
    ASSERT_EQ(lines.size(), 21u);
    EXPECT_EQ(lines.front(), "S2");
    EXPECT_EQ(lines[19], "S21");
    EXPECT_EQ(lines.back(), "\xE2\x80\xA6 and 8 more");
    // One over the cap is listed rather than summarised as "and 1 more".
    lines = legend_layout::overflow_tooltip_lines(std::span<const std::string>(labels), 9, 20);
    EXPECT_EQ(lines.size(), 21u);
    EXPECT_EQ(lines.back(), "S29");
    EXPECT_TRUE(
        legend_layout::overflow_tooltip_lines(std::span<const std::string>(labels), 30).empty());
}

TEST_F(RendererFixture, OverflowCellShowsHiddenNamesOnHoverButNotInExport)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = fig.subplot(3, 1, 1);   // wide but short panel
    auto   x  = ramp(20, 0.0f, 1.0f);
    for (int i = 0; i < 40; ++i)
        ax.plot(x, x, "-").label("Series " + std::to_string(i + 1));

    auto overlay_draws = [&]
    {
        return std::count_if(backend.draws_.begin(),
                             backend.draws_.end(),
                             [](const RecordingBackend::Draw& d)
                             { return d.pipeline == PipelineType::Overlay; });
    };

    render(fig);
    ASSERT_EQ(renderer.legend_overflow_cells().size(), 1u);
    const auto cell = renderer.legend_overflow_cells()[0];
    EXPECT_FALSE(cell.hovered);
    ASSERT_FALSE(cell.hidden.empty());
    // The tooltip starts at the first entry without a swatch (Series K).
    ASSERT_EQ(cell.hidden.front().rfind("Series ", 0), 0u);
    const int  first_hidden = std::stoi(cell.hidden.front().substr(7));
    const auto entries      = legend_layout::legend_entries(ax);
    EXPECT_GT(first_hidden, 1);
    EXPECT_EQ(cell.hidden, legend_layout::overflow_tooltip_lines(entries, first_hidden - 1));
    const auto plain = overlay_draws();

    // Hovering the cell adds the tooltip card.
    renderer.set_pointer(cell.cell.x + 2.0f, cell.cell.y + cell.cell.h * 0.5f);
    render(fig);
    ASSERT_EQ(renderer.legend_overflow_cells().size(), 1u);
    EXPECT_TRUE(renderer.legend_overflow_cells()[0].hovered);
    EXPECT_GT(overlay_draws(), plain);

    // Elsewhere: no tooltip.
    renderer.set_pointer(1.0f, 1.0f);
    render(fig);
    EXPECT_FALSE(renderer.legend_overflow_cells()[0].hovered);
    EXPECT_EQ(overlay_draws(), plain);

    // An export (render_figure) is never a hover state.
    renderer.set_pointer(cell.cell.x + 2.0f, cell.cell.y + cell.cell.h * 0.5f);
    backend.draws_.clear();
    renderer.render_figure(fig);
    EXPECT_FALSE(renderer.legend_overflow_cells()[0].hovered);
}

namespace
{

bool same_rgb(const SeriesPushConstants& pc, const Color& c, float tol = 1e-3f)
{
    return std::abs(pc.color[0] - c.r) < tol && std::abs(pc.color[1] - c.g) < tol
           && std::abs(pc.color[2] - c.b) < tol;
}

// Index of the first legend background draw (theme tooltip colour), or -1.
int legend_bg_index(const std::vector<RecordingBackend::Draw>& draws, const Color& bg)
{
    for (size_t i = 0; i < draws.size(); ++i)
        if (draws[i].pipeline == PipelineType::Overlay && same_rgb(draws[i].pc, bg))
            return static_cast<int>(i);
    return -1;
}

}   // namespace

// Audit S02: a figure with only 3D axes gets a legend in the GPU / PNG export
// path, with swatches in series order and in each series' colour.
TEST_F(RendererFixture, Legend3DOnlyFigureDrawnWithSeriesColours)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = fig.subplot3d(1, 1, 1);
    auto   x  = ramp(20, 0.0f, 1.0f);

    const Color line_c{0.9f, 0.1f, 0.2f, 1.0f};
    const Color scat_c{0.1f, 0.3f, 0.9f, 1.0f};
    const Color unlabeled_c{0.7f, 0.6f, 0.1f, 1.0f};
    ax.line3d(x, x, x).label("trajectory").color(line_c);
    ax.scatter3d(x, x, x).label("samples").color(scat_c);
    ax.scatter3d(x, x, x).color(unlabeled_c);   // no label: no entry
    std::vector<float> g = ramp(8, -1.0f, 1.0f), z(64);
    for (size_t i = 0; i < z.size(); ++i)
        z[i] = static_cast<float>(i) / 64.0f;
    ax.surface(g, g, z).colormap(ColormapType::Jet).label("surface");
    const Color surf_c = SurfaceSeries::sample_colormap(ColormapType::Jet, 0.5f);

    const auto& tc = theme.colors();
    const Color legend_bg{tc.tooltip_bg.r, tc.tooltip_bg.g, tc.tooltip_bg.b, tc.tooltip_bg.a};

    render(fig);
    const auto& d     = backend.draws_;
    const int   first = legend_bg_index(d, legend_bg);
    ASSERT_GE(first, 0) << "3D-only figure must get a legend";

    int line_at = -1, scat_at = -1, surf_at = -1;
    for (size_t i = static_cast<size_t>(first); i < d.size(); ++i)
    {
        const int k = static_cast<int>(i);
        if (line_at < 0 && d[i].pipeline == PipelineType::Line && d[i].count == 6
            && same_rgb(d[i].pc, ui::adapt_series_color_to_theme(line_c, tc)))
            line_at = k;
        if (scat_at < 0 && d[i].pipeline == PipelineType::Scatter
            && same_rgb(d[i].pc, ui::adapt_series_color_to_theme(scat_c, tc)))
            scat_at = k;
        if (surf_at < 0 && d[i].pipeline == PipelineType::Overlay
            && same_rgb(d[i].pc, ui::adapt_series_color_to_theme(surf_c, tc), 2e-3f))
            surf_at = k;
        EXPECT_FALSE(same_rgb(d[i].pc, ui::adapt_series_color_to_theme(unlabeled_c, tc)))
            << "unlabeled series must not get a swatch";
    }
    EXPECT_GE(line_at, 0) << "line swatch (line pipeline) for LineSeries3D";
    EXPECT_GE(scat_at, 0) << "marker swatch for ScatterSeries3D";
    EXPECT_GE(surf_at, 0) << "fill swatch in the surface's colormap colour";
    EXPECT_LT(line_at, scat_at) << "series order";
    EXPECT_LT(scat_at, surf_at) << "series order";

    // With the interactive ImGui legend active the renderer leaves it alone.
    renderer.set_legend_overlay_active(true);
    render(fig);
    EXPECT_LT(legend_bg_index(backend.draws_, legend_bg), 0);
    renderer.set_legend_overlay_active(false);

    fig.legend().visible = false;
    render(fig);
    EXPECT_LT(legend_bg_index(backend.draws_, legend_bg), 0);
}

// Audit S02 + S03: a 3D legend with too many entries for its panel keeps the
// first entries in order, like the 2D legend (the rest go to "+N more").
TEST_F(RendererFixture, Legend3DOverflowKeepsEntriesContiguous)
{
    Figure             fig({.width = 800, .height = 600});
    auto&              ax = fig.subplot3d(3, 1, 1);   // wide but short panel
    auto               x  = ramp(20, 0.0f, 1.0f);
    constexpr int      N  = 40;
    std::vector<Color> colors;
    for (int i = 0; i < N; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(N);
        const Color c{0.2f + 0.6f * t, 0.5f, 0.8f - 0.6f * t, 1.0f};
        colors.push_back(c);
        ax.line3d(x, x, x).label("Series " + std::to_string(i + 1)).color(c);
    }
    const auto& tc = theme.colors();
    const Color legend_bg{tc.tooltip_bg.r, tc.tooltip_bg.g, tc.tooltip_bg.b, tc.tooltip_bg.a};

    render(fig);
    const auto& d     = backend.draws_;
    const int   first = legend_bg_index(d, legend_bg);
    ASSERT_GE(first, 0);
    std::vector<int> drawn;
    for (size_t i = static_cast<size_t>(first); i < d.size(); ++i)
    {
        if (d[i].pipeline != PipelineType::Line || d[i].count != 6)
            continue;
        for (int k = 0; k < N; ++k)
        {
            if (same_rgb(d[i].pc,
                         ui::adapt_series_color_to_theme(colors[static_cast<size_t>(k)], tc),
                         1e-4f))
            {
                drawn.push_back(k);
                break;
            }
        }
    }
    ASSERT_FALSE(drawn.empty());
    ASSERT_LT(drawn.size(), static_cast<size_t>(N)) << "panel is too small for all 40";
    for (size_t k = 0; k < drawn.size(); ++k)
        EXPECT_EQ(drawn[k], static_cast<int>(k)) << "legend entries must be contiguous";
}

// ─── Crowded "Best" legends move outside; "+N more" colour keys ─────────────

namespace
{

// The QA "layout" scene: a 50-entry legend in a small panel.
void add_entries(Axes& ax, int n, const std::string& prefix = "Series ")
{
    const std::vector<float> x = {0.0f, 1.0f};
    for (int i = 0; i < n; ++i)
        ax.line(x, x).label(prefix + std::to_string(i + 1));
}

}   // namespace

TEST(LegendAutoOutside, CoverageGrowsWithEntriesAndShrinkingPlots)
{
    Axes ax;
    add_entries(ax, 50);
    const auto         all = legend_layout::legend_entries(ax);
    const LegendConfig cfg;
    const Rect         small{0.0f, 0.0f, 400.0f, 220.0f};
    const Rect         large{0.0f, 0.0f, 1600.0f, 1000.0f};
    const float        c50_small = legend_layout::inside_coverage(all, cfg, small);
    EXPECT_GT(c50_small, legend_layout::AUTO_OUTSIDE_COVERAGE);
    EXPECT_LE(c50_small, 1.0f);
    EXPECT_LT(legend_layout::inside_coverage(all, cfg, large), c50_small);
    const std::span<Series* const> three(all.data(), 3);
    EXPECT_LT(legend_layout::inside_coverage(three, cfg, small),
              legend_layout::AUTO_OUTSIDE_COVERAGE);
    // Not even one row fits: the inside legend would be omitted (maximal cost).
    EXPECT_EQ(legend_layout::inside_coverage(three, cfg, Rect{0.0f, 0.0f, 400.0f, 10.0f}), 1.0f);
    EXPECT_EQ(legend_layout::inside_coverage({}, cfg, small), 0.0f);
}

TEST(LegendAutoOutside, CrowdedBestLegendReservesOutsideStrip)
{
    Figure fig({.width = 900, .height = 600});
    auto&  ax = fig.subplot(2, 2, 1);
    add_entries(ax, 50);
    fig.compute_layout();
    ASSERT_GT(ax.outside_legend_area().w, 0.0f);
    EXPECT_EQ(legend_layout::effective_position(ax, fig.legend()), LegendPosition::OutsideRight);
    // The strip is right of the plot area and the plot keeps the minimum width.
    EXPECT_GE(ax.outside_legend_area().x, ax.viewport().x + ax.viewport().w - 0.5f);
    EXPECT_GE(ax.viewport().w, legend_layout::AUTO_OUTSIDE_MIN_PLOT_W - 1.0f);
}

TEST(LegendAutoOutside, ExplicitPositionsAndSmallLegendsAreNotMoved)
{
    for (LegendPosition pos : {LegendPosition::TopRight,
                               LegendPosition::TopLeft,
                               LegendPosition::BottomLeft,
                               LegendPosition::BottomRight})
    {
        Figure fig({.width = 900, .height = 600});
        auto&  ax = fig.subplot(2, 2, 1);
        add_entries(ax, 50);
        fig.legend().position = pos;
        fig.compute_layout();
        EXPECT_EQ(ax.outside_legend_area().w, 0.0f) << static_cast<int>(pos);
        EXPECT_EQ(legend_layout::effective_position(ax, fig.legend()), pos);
    }

    // A handful of entries in a normal plot: compact inside legend, unchanged layout.
    Figure fig({.width = 900, .height = 600});
    auto&  ax = fig.subplot(1, 1, 1);
    add_entries(ax, 4);
    fig.compute_layout();
    EXPECT_EQ(ax.outside_legend_area().w, 0.0f);
    EXPECT_EQ(legend_layout::effective_position(ax, fig.legend()), LegendPosition::Best);
}

TEST(LegendAutoOutside, NoRoomKeepsCompactSummary)
{
    // Too narrow to give up a strip: the plot would drop below the minimum width.
    Figure fig({.width = 320, .height = 260});
    auto&  ax = fig.subplot(1, 1, 1);
    add_entries(ax, 50);
    fig.compute_layout();
    EXPECT_EQ(ax.outside_legend_area().w, 0.0f);
    EXPECT_EQ(legend_layout::effective_position(ax, fig.legend()), LegendPosition::Best);

    // A hidden legend reserves nothing.
    Figure hidden({.width = 900, .height = 600});
    auto&  hx = hidden.subplot(2, 2, 1);
    add_entries(hx, 50);
    hidden.legend().visible = false;
    hidden.compute_layout();
    EXPECT_EQ(hx.outside_legend_area().w, 0.0f);
}

TEST_F(RendererFixture, AutoOutsideLegendIsDrawnInTheStrip)
{
    Figure fig({.width = 900, .height = 600});
    auto&  ax = fig.subplot(2, 2, 1);
    add_entries(ax, 50);
    render(fig);
    const Rect strip = ax.outside_legend_area();
    ASSERT_GT(strip.w, 0.0f);
    // The one-column strip still cannot hold all 50: its "+N more" cell is in
    // the strip, right of the plot area, not over the data.
    ASSERT_EQ(renderer.legend_overflow_cells().size(), 1u);
    const Rect cell = renderer.legend_overflow_cells()[0].cell;
    EXPECT_GE(cell.x, ax.viewport().x + ax.viewport().w);
    EXPECT_GE(cell.x, strip.x);
    EXPECT_LE(cell.y + cell.h, strip.y + strip.h + 0.5f);
}

// S4-U01: the headless/PNG legend applies the same plot-area cap as the SVG
// and window legends to an explicitly placed dense legend (QA gallery
// "layout" scene), so most of the plot stays visible.
TEST_F(RendererFixture, DenseExplicitLegendIsCappedInsideThePlot)
{
    Figure fig({.width = 640, .height = 480});
    auto&  ax                  = fig.subplot(1, 1, 1);
    fig.legend().position      = LegendPosition::TopRight;
    fig.legend().font_size     = 12.0f;
    const std::vector<float> x = {0.0f, 1.0f};
    for (int i = 0; i < 50; ++i)
        ax.line(x, x).label("Sensor " + std::to_string(i + 1) + " / descriptive long legend entry");
    render(fig);
    ASSERT_EQ(renderer.legend_overflow_cells().size(), 1u);
    const auto& cell = renderer.legend_overflow_cells()[0];
    const Rect& vp   = ax.viewport();
    // The "+N more" cell (the last row of the box) sits in the top part of the
    // plot: the box no longer runs down most of its height.
    EXPECT_LT(cell.cell.y + cell.cell.h - vp.y, 0.6f * vp.h);
    EXPECT_GE(cell.cell.x + cell.cell.w, vp.x + 0.5f * vp.w) << "TopRight is kept";
    ASSERT_FALSE(cell.hidden.empty());
    // Entries before the cell are listed in order; at least INSIDE_MIN_ROWS - 1 are shown.
    const int first_hidden = std::stoi(cell.hidden.front().substr(7)) - 1;   // "Sensor N / ..."
    EXPECT_GE(first_hidden, legend_layout::INSIDE_MIN_ROWS - 1);
    EXPECT_LT(first_hidden, 50);
}

TEST_F(RendererFixture, OverflowTooltipCarriesColourKeysOfHiddenEntries)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax                  = fig.subplot(3, 1, 1);
    fig.legend().position      = LegendPosition::TopLeft;   // explicit: stays compact
    const std::vector<float> x = {0.0f, 1.0f};
    for (int i = 0; i < 40; ++i)
    {
        const Color c{static_cast<float>(i) / 40.0f, 0.5f, 1.0f - static_cast<float>(i) / 40.0f};
        ax.line(x, x).label("Series " + std::to_string(i + 1)).color(c);
    }
    ax.series_mut()[39]->visible(false);   // hidden series stay listed, dimmed
    render(fig);
    ASSERT_EQ(renderer.legend_overflow_cells().size(), 1u);
    const auto& cell    = renderer.legend_overflow_cells()[0];
    const auto  entries = legend_layout::legend_entries(ax);
    ASSERT_FALSE(cell.hidden.empty());
    const size_t first        = static_cast<size_t>(std::stoi(cell.hidden.front().substr(7))) - 1;
    const size_t hidden_total = entries.size() - first;
    // Every listed entry has its colour key; a trailing "and K more" line has none.
    const bool summarised = cell.hidden.size() != hidden_total;
    ASSERT_EQ(cell.colors.size(), cell.hidden.size() - (summarised ? 1u : 0u));
    for (size_t k = 0; k < cell.colors.size(); ++k)
    {
        const Series& s = *entries[first + k];
        const Color   want =
            ui::adapt_series_color_to_theme(legend_layout::swatch_color(s), theme.colors());
        EXPECT_NEAR(cell.colors[k].r, want.r, 1e-5f) << k;
        EXPECT_NEAR(cell.colors[k].b, want.b, 1e-5f) << k;
        EXPECT_LT(cell.colors[k].a, s.visible() ? 1.01f : 0.5f) << k;
    }
}
