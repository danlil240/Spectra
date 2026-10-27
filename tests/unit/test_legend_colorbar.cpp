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
