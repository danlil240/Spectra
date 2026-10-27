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
    explicit SubsetBackend(bool webgpu_subset) : webgpu_subset_(webgpu_subset) {}

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
        return PipelineHandle{next_id_++};
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
    void     bind_pipeline(PipelineHandle) override {}
    void     bind_buffer(BufferHandle, uint32_t) override {}
    void     bind_index_buffer(BufferHandle) override {}
    void     bind_texture(TextureHandle, uint32_t) override {}
    void     push_constants(const SeriesPushConstants&) override {}
    void     set_viewport(float, float, float, float) override {}
    void     set_scissor(int32_t, int32_t, uint32_t, uint32_t) override {}
    void     set_line_width(float) override {}
    void     draw(uint32_t, uint32_t) override {}
    void     draw_instanced(uint32_t, uint32_t, uint32_t, uint32_t) override {}
    void     draw_indexed(uint32_t, uint32_t, int32_t) override {}
    bool     readback_framebuffer(uint8_t*, uint32_t, uint32_t) override { return false; }
    uint32_t swapchain_width() const override { return 0; }
    uint32_t swapchain_height() const override { return 0; }

   private:
    bool     webgpu_subset_;
    uint64_t next_id_ = 1;
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

    EXPECT_FALSE(webgpu_supports_pipeline(PipelineType::ScatterColormap));
    EXPECT_FALSE(webgpu_supports_pipeline(PipelineType::Heatmap));
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

TEST(RenderUnsupportedSeries, SubsetBackendRecordsColormapFallback)
{
    SubsetBackend    backend(/*webgpu_subset=*/true);
    ui::ThemeManager theme;
    Renderer         renderer(backend, theme);
    ASSERT_TRUE(renderer.init());

    Figure             fig;
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x  = {0.0f, 1.0f, 2.0f};
    std::vector<float> y  = {0.0f, 1.0f, 0.5f};
    std::vector<float> v  = {0.0f, 0.5f, 1.0f};
    ax.scatter(x, y).color_values(v).colormap(ColormapType::Viridis);
    fig.compute_layout();

    renderer.render_figure_content(fig);
    ASSERT_EQ(renderer.unsupported_series().size(), 1u);
    EXPECT_EQ(renderer.unsupported_series()[0], "colormapped scatter (drawn in one color)");
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
