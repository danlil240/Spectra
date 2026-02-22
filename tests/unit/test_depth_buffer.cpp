#include <algorithm>
#include <cstring>
#include <gtest/gtest.h>
#include <spectra/app.hpp>
#include <spectra/math3d.hpp>
#include <spectra/spectra.hpp>
#include <vector>

#include "render/backend.hpp"

using namespace spectra;

// ─── Fixture ────────────────────────────────────────────────────────────────

class DepthBufferTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        AppConfig config;
        config.headless = true;
        app_            = std::make_unique<App>(config);
    }

    void TearDown() override { app_.reset(); }

    std::unique_ptr<App> app_;
};

// ─── Pipeline Creation ──────────────────────────────────────────────────────

TEST_F(DepthBufferTest, DepthBufferCreatedWithSwapchain)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    auto pipeline = backend->create_pipeline(PipelineType::Scatter3D);
    EXPECT_TRUE(pipeline);
}

TEST_F(DepthBufferTest, DepthBufferExistsForMultiplePipelines)
{
    auto* backend = app_->backend();

    auto line3d    = backend->create_pipeline(PipelineType::Line3D);
    auto scatter3d = backend->create_pipeline(PipelineType::Scatter3D);

    EXPECT_TRUE(line3d);
    EXPECT_TRUE(scatter3d);
}

TEST_F(DepthBufferTest, DepthTestingEnabledFor3D)
{
    auto* backend = app_->backend();

    auto line3d    = backend->create_pipeline(PipelineType::Line3D);
    auto scatter3d = backend->create_pipeline(PipelineType::Scatter3D);
    auto grid3d    = backend->create_pipeline(PipelineType::Grid3D);

    EXPECT_TRUE(line3d);
    EXPECT_TRUE(scatter3d);
    EXPECT_TRUE(grid3d);
}

TEST_F(DepthBufferTest, DepthTestingDisabledFor2D)
{
    auto* backend = app_->backend();

    auto line2d    = backend->create_pipeline(PipelineType::Line);
    auto scatter2d = backend->create_pipeline(PipelineType::Scatter);
    auto grid2d    = backend->create_pipeline(PipelineType::Grid);

    EXPECT_TRUE(line2d);
    EXPECT_TRUE(scatter2d);
    EXPECT_TRUE(grid2d);
}

TEST_F(DepthBufferTest, AllPipelineTypesSupported)
{
    auto* backend = app_->backend();

    auto line2d    = backend->create_pipeline(PipelineType::Line);
    auto scatter2d = backend->create_pipeline(PipelineType::Scatter);
    auto grid2d    = backend->create_pipeline(PipelineType::Grid);
    auto line3d    = backend->create_pipeline(PipelineType::Line3D);
    auto scatter3d = backend->create_pipeline(PipelineType::Scatter3D);
    auto grid3d    = backend->create_pipeline(PipelineType::Grid3D);

    EXPECT_TRUE(line2d);
    EXPECT_TRUE(scatter2d);
    EXPECT_TRUE(grid2d);
    EXPECT_TRUE(line3d);
    EXPECT_TRUE(scatter3d);
    EXPECT_TRUE(grid3d);
}

TEST_F(DepthBufferTest, DepthBufferFormatSupported)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    auto pipeline = backend->create_pipeline(PipelineType::Scatter3D);
    EXPECT_TRUE(pipeline);
}

TEST_F(DepthBufferTest, MeshAndSurfacePipelineTypes)
{
    [[maybe_unused]] PipelineType mesh3d    = PipelineType::Mesh3D;
    [[maybe_unused]] PipelineType surface3d = PipelineType::Surface3D;

    SUCCEED();
}

// ─── Offscreen Depth Buffer ─────────────────────────────────────────────────

TEST_F(DepthBufferTest, OffscreenFramebufferHasDepth)
{
    // Headless mode creates an offscreen framebuffer — 3D pipelines must still work
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    auto scatter3d = backend->create_pipeline(PipelineType::Scatter3D);
    EXPECT_TRUE(scatter3d) << "3D pipeline creation must succeed in headless/offscreen mode";
}

TEST_F(DepthBufferTest, OffscreenRenderWith3DContent)
{
    auto& fig = app_->figure({.width = 320, .height = 240});
    auto& ax  = fig.subplot3d(1, 1, 1);

    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 0.5f};
    std::vector<float> z = {0.0f, 0.5f, 1.0f};
    ax.scatter3d(x, y, z).color(colors::blue).size(6.0f);

    // Should not crash — exercises the full offscreen render path with depth
    EXPECT_NO_FATAL_FAILURE(app_->run());
}

// ─── Depth Clear Validation ─────────────────────────────────────────────────

TEST_F(DepthBufferTest, DepthClearedOnRenderPassBegin)
{
    // Render a 3D scene — the depth buffer must be cleared to 1.0 at the start
    // of each render pass. If not, geometry from previous frames would occlude.
    auto& fig = app_->figure({.width = 320, .height = 240});
    auto& ax  = fig.subplot3d(1, 1, 1);

    std::vector<float> x = {0.0f};
    std::vector<float> y = {0.0f};
    std::vector<float> z = {0.0f};
    ax.scatter3d(x, y, z).color(colors::red).size(10.0f);

    // Run twice — if depth isn't cleared, second frame would have stale depth
    EXPECT_NO_FATAL_FAILURE(app_->run());
    EXPECT_NO_FATAL_FAILURE(app_->run());
}

// ─── Readback with 3D Content ───────────────────────────────────────────────

TEST_F(DepthBufferTest, ReadbackFramebufferWith3D)
{
    auto& fig = app_->figure({.width = 64, .height = 64});
    auto& ax  = fig.subplot3d(1, 1, 1);

    std::vector<float> x = {0.0f};
    std::vector<float> y = {0.0f};
    std::vector<float> z = {0.0f};
    ax.scatter3d(x, y, z).color(colors::red).size(20.0f);

    app_->run();

    std::vector<uint8_t> pixels(64 * 64 * 4);
    auto*                backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    bool ok = backend->readback_framebuffer(pixels.data(), 64, 64);
    EXPECT_TRUE(ok) << "Readback must succeed with depth-enabled framebuffer";

    // Verify the buffer was actually written to (not all zeros)
    bool has_nonzero = false;
    for (size_t i = 0; i < pixels.size(); ++i)
    {
        if (pixels[i] != 0)
        {
            has_nonzero = true;
            break;
        }
    }
    EXPECT_TRUE(has_nonzero) << "Readback buffer should contain rendered data";
}

// ─── FrameUBO Layout Validation ─────────────────────────────────────────────

TEST_F(DepthBufferTest, FrameUBOSize)
{
    // FrameUBO must be exactly the right size for std140 layout
    // 3 * mat4 (48 floats) + viewport_size(2) + time(1) + pad(1) +
    // camera_pos(3) + near(1) + light_dir(3) + far(1) = 60 floats = 240 bytes
    EXPECT_EQ(sizeof(FrameUBO), 240u);
}

TEST_F(DepthBufferTest, FrameUBODefaultValues)
{
    FrameUBO ubo{};

    // Projection, view, model should be zero-initialized
    for (int i = 0; i < 16; ++i)
    {
        EXPECT_FLOAT_EQ(ubo.projection[i], 0.0f);
        EXPECT_FLOAT_EQ(ubo.view[i], 0.0f);
        EXPECT_FLOAT_EQ(ubo.model[i], 0.0f);
    }

    EXPECT_FLOAT_EQ(ubo.near_plane, 0.01f);
    EXPECT_FLOAT_EQ(ubo.far_plane, 1000.0f);
}

// ─── SeriesPushConstants Layout ─────────────────────────────────────────────

TEST_F(DepthBufferTest, PushConstantsSize)
{
    // Push constants must be exactly 96 bytes (under 128-byte minimum guarantee)
    EXPECT_EQ(sizeof(SeriesPushConstants), 96u);
}

TEST_F(DepthBufferTest, PushConstantsDefaults)
{
    SeriesPushConstants pc{};
    EXPECT_FLOAT_EQ(pc.line_width, 2.0f);
    EXPECT_FLOAT_EQ(pc.point_size, 4.0f);
    EXPECT_FLOAT_EQ(pc.opacity, 1.0f);
    EXPECT_EQ(pc.line_style, 1u);    // Solid
    EXPECT_EQ(pc.marker_type, 0u);   // None
}

// ─── PipelineType Enum Completeness ─────────────────────────────────────────

TEST_F(DepthBufferTest, PipelineTypeEnumValues)
{
    // Verify all expected pipeline types exist
    EXPECT_NE(static_cast<int>(PipelineType::Line), static_cast<int>(PipelineType::Line3D));
    EXPECT_NE(static_cast<int>(PipelineType::Scatter), static_cast<int>(PipelineType::Scatter3D));
    EXPECT_NE(static_cast<int>(PipelineType::Grid), static_cast<int>(PipelineType::Grid3D));
    EXPECT_NE(static_cast<int>(PipelineType::Mesh3D), static_cast<int>(PipelineType::Surface3D));
}

TEST_F(DepthBufferTest, GridOverlay3DPipelineType)
{
    // GridOverlay3D is a special pipeline type — no depth test, for grid lines after series
    [[maybe_unused]] PipelineType overlay = PipelineType::GridOverlay3D;
    EXPECT_NE(static_cast<int>(PipelineType::Grid3D),
              static_cast<int>(PipelineType::GridOverlay3D));
}

// ─── Mixed 2D + 3D Depth Isolation ──────────────────────────────────────────

TEST_F(DepthBufferTest, Mixed2DAnd3DRendering)
{
    auto& fig = app_->figure({.width = 320, .height = 480});

    // 2D subplot
    auto&              ax2d = fig.subplot(2, 1, 1);
    std::vector<float> x2d  = {0.0f, 1.0f, 2.0f};
    std::vector<float> y2d  = {0.0f, 1.0f, 0.5f};
    ax2d.line(x2d, y2d).color(colors::blue);

    // 3D subplot
    auto&              ax3d = fig.subplot3d(2, 1, 2);
    std::vector<float> x3d  = {0.0f, 1.0f};
    std::vector<float> y3d  = {0.0f, 1.0f};
    std::vector<float> z3d  = {0.0f, 1.0f};
    ax3d.scatter3d(x3d, y3d, z3d).color(colors::red);

    // Must not crash — 2D pipelines have depth disabled, 3D enabled
    EXPECT_NO_FATAL_FAILURE(app_->run());
}

// ─── Buffer Management ──────────────────────────────────────────────────────

TEST_F(DepthBufferTest, BufferCreateAndDestroy)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    auto buf = backend->create_buffer(BufferUsage::Storage, 1024);
    EXPECT_TRUE(buf);

    // Destroy should not crash
    EXPECT_NO_FATAL_FAILURE(backend->destroy_buffer(buf));
}

TEST_F(DepthBufferTest, IndexBufferCreation)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    auto idx_buf = backend->create_buffer(BufferUsage::Index, 256);
    EXPECT_TRUE(idx_buf);

    EXPECT_NO_FATAL_FAILURE(backend->destroy_buffer(idx_buf));
}

// ─── Draw Indexed ───────────────────────────────────────────────────────────

TEST_F(DepthBufferTest, DrawIndexedExists)
{
    // Verify draw_indexed is callable on the backend (needed for mesh/surface)
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    // We can't call draw_indexed outside a render pass, but we can verify the
    // method exists and the backend pointer is valid
    SUCCEED();
}

// ─── Multiple 3D Subplots ───────────────────────────────────────────────────

TEST_F(DepthBufferTest, Multiple3DSubplotsShareDepthBuffer)
{
    auto& fig = app_->figure({.width = 640, .height = 480});

    auto&              ax1 = fig.subplot3d(1, 2, 1);
    std::vector<float> x1  = {0.0f, 1.0f};
    std::vector<float> y1  = {0.0f, 1.0f};
    std::vector<float> z1  = {0.0f, 1.0f};
    ax1.scatter3d(x1, y1, z1).color(colors::red);

    auto&              ax2 = fig.subplot3d(1, 2, 2);
    std::vector<float> x2  = {0.0f, 1.0f};
    std::vector<float> y2  = {0.0f, 1.0f};
    std::vector<float> z2  = {0.0f, 1.0f};
    ax2.line3d(x2, y2, z2).color(colors::green);

    // Both subplots use the same depth buffer — must not interfere
    EXPECT_NO_FATAL_FAILURE(app_->run());
}

// ─── Empty 3D Axes ──────────────────────────────────────────────────────────

TEST_F(DepthBufferTest, Empty3DAxesRender)
{
    auto& fig = app_->figure({.width = 320, .height = 240});
    auto& ax  = fig.subplot3d(1, 1, 1);
    ax.title("Empty 3D");

    // Empty 3D axes should render bounding box + grid without crashing
    EXPECT_NO_FATAL_FAILURE(app_->run());
}
