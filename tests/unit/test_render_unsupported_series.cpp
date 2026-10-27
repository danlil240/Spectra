// Renderer behaviour when the backend has no pipeline for a series type
// (the experimental WebGPU backend only builds a 2D subset): the series is
// skipped, recorded in Renderer::unsupported_series() and reported through an
// in-canvas notice instead of silently disappearing.
//
// Uses a CPU-only fake backend that mirrors the WebGPU supported set, so it
// runs in the default (non-WebGPU) build.

#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <string_view>
#include <utility>
#include <vector>

#include "render/backend.hpp"
#include "render/renderer.hpp"
#include "render/webgpu/wgpu_pipeline_support.hpp"
#include "ui/theme/theme.hpp"

using namespace spectra;

namespace
{

class SubsetBackend : public Backend
{
   public:
    explicit SubsetBackend(bool webgpu_subset, PipelineType also_missing = PipelineType::Image3D)
        : webgpu_subset_(webgpu_subset), also_missing_(also_missing)
    {
    }

    bool init(bool) override { return true; }
    void shutdown() override {}
    void wait_idle() override {}
    bool create_surface(void*) override { return true; }
    bool create_swapchain(uint32_t, uint32_t) override { return true; }
    bool recreate_swapchain(uint32_t, uint32_t) override { return true; }
    bool create_offscreen_framebuffer(uint32_t, uint32_t) override { return true; }

    PipelineHandle create_pipeline(PipelineType type) override
    {
        if (webgpu_subset_ && !webgpu_supports_pipeline(type))
            return PipelineHandle{};
        if (type == also_missing_)
            return PipelineHandle{};
        PipelineHandle h{next_id_++};
        pipeline_types_.push_back({h.id, type});
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

    bool     begin_frame(FrameProfiler*) override { return true; }
    void     end_frame(FrameProfiler*) override {}
    void     begin_render_pass(const Color&) override {}
    void     end_render_pass() override {}
    void     bind_pipeline(PipelineHandle p) override
    {
        bound_ = PipelineType::Image3D;
        for (const auto& [id, type] : pipeline_types_)
        {
            if (id == p.id)
                bound_ = type;
        }
    }
    void     bind_buffer(BufferHandle, uint32_t) override {}
    void     bind_index_buffer(BufferHandle) override {}
    void     bind_texture(TextureHandle, uint32_t) override {}
    void     push_constants(const SeriesPushConstants&) override {}
    void     set_viewport(float, float, float, float) override {}
    void     set_scissor(int32_t, int32_t, uint32_t, uint32_t) override {}
    void     set_line_width(float) override {}
    void     draw(uint32_t vertex_count, uint32_t) override
    {
        if (bound_ == PipelineType::Text)
            text_vertices += vertex_count;
    }
    void draw_instanced(uint32_t, uint32_t, uint32_t, uint32_t) override
    {
        if (bound_ == PipelineType::ScatterColormap)
            ++colormap_draws;
    }
    void     draw_indexed(uint32_t, uint32_t, int32_t) override {}
    bool     readback_framebuffer(uint8_t*, uint32_t, uint32_t) override { return false; }
    uint32_t swapchain_width() const override { return 0; }
    uint32_t swapchain_height() const override { return 0; }
    // WebGPU clip space is Y-up (the Vulkan default is Y-down).
    bool clip_y_down() const override { return !webgpu_subset_; }

    uint32_t text_vertices  = 0;
    uint32_t colormap_draws = 0;

   private:
    bool                                           webgpu_subset_;
    PipelineType                                   also_missing_;
    uint64_t                                       next_id_ = 1;
    std::vector<std::pair<uint64_t, PipelineType>> pipeline_types_;
    PipelineType                                   bound_ = PipelineType::Image3D;
};

bool contains(const std::vector<std::string_view>& v, std::string_view s)
{
    for (auto e : v)
    {
        if (e == s)
            return true;
    }
    return false;
}

void add_surface(Figure& fig)
{
    auto&              ax = fig.subplot3d(1, 1, 1);
    std::vector<float> xs = {0.0f, 1.0f, 2.0f};
    std::vector<float> ys = {0.0f, 1.0f, 2.0f};
    std::vector<float> zs = {0.0f, 1.0f, 0.0f, 1.0f, 2.0f, 1.0f, 0.0f, 1.0f, 0.0f};
    ax.surface(xs, ys, zs);
    std::vector<float> lx = {0.0f, 1.0f, 2.0f};
    ax.line3d(lx, lx, lx);
}

}   // namespace

TEST(WebGPUPipelineSupport, SupportedSetIs2DSubset)
{
    EXPECT_TRUE(webgpu_supports_pipeline(PipelineType::Line));
    EXPECT_TRUE(webgpu_supports_pipeline(PipelineType::Scatter));
    EXPECT_TRUE(webgpu_supports_pipeline(PipelineType::Grid));
    EXPECT_TRUE(webgpu_supports_pipeline(PipelineType::Overlay));
    EXPECT_TRUE(webgpu_supports_pipeline(PipelineType::StatFill));
    EXPECT_TRUE(webgpu_supports_pipeline(PipelineType::Text));
    EXPECT_TRUE(webgpu_supports_pipeline(PipelineType::TextDepth));

    EXPECT_TRUE(webgpu_supports_pipeline(PipelineType::ScatterColormap));
    EXPECT_TRUE(webgpu_supports_pipeline(PipelineType::Heatmap));
    EXPECT_FALSE(webgpu_supports_pipeline(PipelineType::Line3D));
    EXPECT_FALSE(webgpu_supports_pipeline(PipelineType::Scatter3D));
    EXPECT_FALSE(webgpu_supports_pipeline(PipelineType::Surface3D));
    EXPECT_FALSE(webgpu_supports_pipeline(PipelineType::Mesh3D));
    EXPECT_FALSE(webgpu_supports_pipeline(PipelineType::Grid3D));
}

TEST(RenderUnsupportedSeries, NoticeTextListsKinds)
{
    EXPECT_EQ(Renderer::format_unsupported_notice({}), "");
    const std::vector<std::string_view> kinds = {"3D axes", "3D surface"};
    EXPECT_EQ(Renderer::format_unsupported_notice(kinds),
              "Not supported by this render backend: 3D axes, 3D surface");
}

TEST(RenderUnsupportedSeries, SubsetBackendRecords3DSeries)
{
    SubsetBackend    backend(/*webgpu_subset=*/true);
    ui::ThemeManager theme;
    Renderer         renderer(backend, theme);
    ASSERT_TRUE(renderer.init());

    Figure fig;
    add_surface(fig);
    fig.compute_layout();

    renderer.render_figure_content(fig);
    const auto& kinds = renderer.unsupported_series();
    EXPECT_TRUE(contains(kinds, "3D axes"));
    EXPECT_TRUE(contains(kinds, "3D surface"));
    EXPECT_TRUE(contains(kinds, "3D line"));
    EXPECT_EQ(kinds.size(), 3u);   // each kind listed once

    // The list is per figure render, not cumulative.
    Figure fig2d;
    auto&  ax = fig2d.subplot(1, 1, 1);
    ax.line(std::vector<float>{0.0f, 1.0f}, std::vector<float>{0.0f, 1.0f});
    fig2d.compute_layout();
    renderer.render_figure_content(fig2d);
    EXPECT_TRUE(renderer.unsupported_series().empty());
}

void add_colormap_scatter(Figure& fig)
{
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x  = {0.0f, 1.0f, 2.0f};
    std::vector<float> y  = {0.0f, 1.0f, 0.5f};
    std::vector<float> v  = {0.0f, 0.5f, 1.0f};
    ax.scatter(x, y).color_values(v).colormap(ColormapType::Viridis);
}

TEST(RenderUnsupportedSeries, SubsetBackendDrawsColormapScatter)
{
    // The WebGPU subset has a ScatterColormap pipeline: no notice, and the
    // colormapped draw goes through it.
    SubsetBackend    backend(/*webgpu_subset=*/true);
    ui::ThemeManager theme;
    Renderer         renderer(backend, theme);
    ASSERT_TRUE(renderer.init());

    Figure fig;
    add_colormap_scatter(fig);
    fig.compute_layout();

    renderer.render_figure_content(fig);
    EXPECT_TRUE(renderer.unsupported_series().empty());
    EXPECT_EQ(backend.colormap_draws, 1u);
}

TEST(RenderUnsupportedSeries, MissingColormapPipelineFallsBack)
{
    SubsetBackend    backend(/*webgpu_subset=*/true, PipelineType::ScatterColormap);
    ui::ThemeManager theme;
    Renderer         renderer(backend, theme);
    ASSERT_TRUE(renderer.init());

    Figure fig;
    add_colormap_scatter(fig);
    fig.compute_layout();

    renderer.render_figure_content(fig);
    ASSERT_EQ(renderer.unsupported_series().size(), 1u);
    EXPECT_EQ(renderer.unsupported_series()[0], "colormapped scatter (drawn in one color)");
    EXPECT_EQ(backend.colormap_draws, 0u);
}

// Text glyph vertices drawn for one 640x480 frame on the WebGPU subset.
uint32_t webgpu_text_vertices(bool with_labels)
{
    SubsetBackend    backend(/*webgpu_subset=*/true);
    ui::ThemeManager theme;
    ui::ThemeManager::set_current(&theme);
    theme.ensure_initialized();
    Renderer renderer(backend, theme);
    EXPECT_TRUE(renderer.init());

    Figure             fig({.width = 640, .height = 480});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x  = {0.0f, 10.0f};
    std::vector<float> y  = {-1.0f, 1.0f};
    ax.line(x, y);
    if (with_labels)
    {
        ax.xlabel("Time");
        ax.ylabel("Amplitude");
    }
    fig.compute_layout();
    renderer.render_figure(fig);
    ui::ThemeManager::set_current(nullptr);
    return backend.text_vertices;
}

TEST(RenderWebGPUSubset, AxisLabelsAreDrawn)
{
    // Regression for #111: a headless WebGPU render showed no axis labels.
    // Labels go through the shared Text pipeline, so they must add glyphs.
    const uint32_t without = webgpu_text_vertices(false);
    const uint32_t with    = webgpu_text_vertices(true);
    EXPECT_GT(without, 0u);   // tick labels
    // "Time" + "Amplitude" = 13 glyphs, 6 vertices each.
    EXPECT_GE(with, without + 13u * 6u);
}

TEST(RenderWebGPUSubset, TickDensityFollowsCanvasSize)
{
    // Regression for #111: the browser render showed only three ticks per
    // axis.  Tick density is pixel based, so a 640x480 canvas gets more.
    Figure             fig({.width = 640, .height = 480});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x  = {0.0f, 10.0f};
    std::vector<float> y  = {-1.0f, 1.0f};
    ax.line(x, y);
    ax.xlim(0.0, 10.0);
    ax.ylim(-1.0, 1.0);
    fig.compute_layout();
    EXPECT_GE(ax.compute_x_ticks().positions.size(), 5u);
    EXPECT_GE(ax.compute_y_ticks().positions.size(), 4u);
}

TEST(RenderUnsupportedSeries, FullBackendRecordsNothing)
{
    SubsetBackend    backend(/*webgpu_subset=*/false);
    ui::ThemeManager theme;
    Renderer         renderer(backend, theme);
    ASSERT_TRUE(renderer.init());

    Figure fig;
    add_surface(fig);
    fig.compute_layout();

    renderer.render_figure_content(fig);
    EXPECT_TRUE(renderer.unsupported_series().empty());
}
