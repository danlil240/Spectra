#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <spectra/app.hpp>
#include <spectra/figure.hpp>
#include <spectra/spectra.hpp>
#include <vector>

#include "gpu_hang_detector.hpp"
#include "multi_window_fixture.hpp"
#include "render/backend.hpp"

using namespace spectra;
using namespace spectra::test;

// ═══════════════════════════════════════════════════════════════════════════════
// PHASE 0 — Single-Window Regression (always runs)
// These tests verify that the current single-window codebase is healthy.
// They serve as the regression baseline after each agent merge.
// ═══════════════════════════════════════════════════════════════════════════════

// ─── SingleWindowRegression ──────────────────────────────────────────────────

TEST_F(SingleWindowFixture, HeadlessAppCreation)
{
    ASSERT_NE(app_.get(), nullptr);
    EXPECT_TRUE(app_->is_headless());
}

TEST_F(SingleWindowFixture, BackendInitialized)
{
    ASSERT_NE(app_->backend(), nullptr);
}

TEST_F(SingleWindowFixture, RendererInitialized)
{
    ASSERT_NE(app_->renderer(), nullptr);
}

TEST_F(SingleWindowFixture, SingleFigureCreation)
{
    auto& fig = create_simple_figure();
    EXPECT_EQ(fig.width(), 640u);
    EXPECT_EQ(fig.height(), 480u);
    EXPECT_EQ(fig.axes().size(), 1u);
}

TEST_F(SingleWindowFixture, RenderOneFrame)
{
    create_simple_figure();
    EXPECT_TRUE(render_one_frame());
}

TEST_F(SingleWindowFixture, RenderProducesPixels)
{
    auto& fig = create_simple_figure();
    app_->run();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(readback(fig, pixels));
    EXPECT_TRUE(has_non_zero_pixels(pixels));
}

TEST_F(SingleWindowFixture, MultipleFiguresHeadless)
{
    auto& fig1 = app_->figure({.width = 320, .height = 240});
    auto& ax1 = fig1.subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y1 = {0.0f, 1.0f, 0.5f};
    ax1.line(x, y1);

    auto& fig2 = app_->figure({.width = 320, .height = 240});
    auto& ax2 = fig2.subplot(1, 1, 1);
    std::vector<float> y2 = {1.0f, 0.0f, 1.5f};
    ax2.line(x, y2);

    EXPECT_TRUE(render_one_frame());
}

TEST_F(SingleWindowFixture, PipelineCreation2D)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    auto line = backend->create_pipeline(PipelineType::Line);
    auto scatter = backend->create_pipeline(PipelineType::Scatter);
    auto grid = backend->create_pipeline(PipelineType::Grid);

    EXPECT_TRUE(line);
    EXPECT_TRUE(scatter);
    EXPECT_TRUE(grid);
}

TEST_F(SingleWindowFixture, PipelineCreation3D)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    auto line3d = backend->create_pipeline(PipelineType::Line3D);
    auto scatter3d = backend->create_pipeline(PipelineType::Scatter3D);
    auto mesh3d = backend->create_pipeline(PipelineType::Mesh3D);
    auto surface3d = backend->create_pipeline(PipelineType::Surface3D);

    EXPECT_TRUE(line3d);
    EXPECT_TRUE(scatter3d);
    EXPECT_TRUE(mesh3d);
    EXPECT_TRUE(surface3d);
}

TEST_F(SingleWindowFixture, BufferCreateDestroy)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    auto buf = backend->create_buffer(BufferUsage::Storage, 1024);
    EXPECT_TRUE(buf);
    backend->destroy_buffer(buf);
}

TEST_F(SingleWindowFixture, FrameUBOLayout)
{
    EXPECT_EQ(sizeof(FrameUBO), 240u);
}

TEST_F(SingleWindowFixture, PushConstantsLayout)
{
    EXPECT_EQ(sizeof(SeriesPushConstants), 96u);
}

TEST_F(SingleWindowFixture, RenderNoHang)
{
    create_simple_figure();
    GpuHangDetector detector(std::chrono::seconds(10));
    detector.expect_no_hang("single window render", [&]() { app_->run(); });
}

// ─── Resize Regression ──────────────────────────────────────────────────────

TEST_F(SingleWindowFixture, OffscreenFramebufferCreation)
{
    auto& fig = create_simple_figure();
    app_->run();

    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);
    // After rendering, offscreen framebuffer should have the figure's dimensions
    EXPECT_EQ(backend->swapchain_width(), fig.width());
    EXPECT_EQ(backend->swapchain_height(), fig.height());
}

// ═══════════════════════════════════════════════════════════════════════════════
// PHASE 1 — WindowContext Extraction (after Agent A merge)
// Tests that WindowContext struct exists and single-window behavior is preserved.
// ═══════════════════════════════════════════════════════════════════════════════

#ifdef SPECTRA_HAS_WINDOW_CONTEXT

TEST(WindowContextPhase1, StructExists)
{
    // Verify WindowContext can be instantiated
    // WindowContext wctx{};
    // EXPECT_EQ(wctx.id, 0u);
    GTEST_SKIP() << "WindowContext not yet implemented (Agent A)";
}

TEST(WindowContextPhase1, SetActiveWindow)
{
    // Verify VulkanBackend::set_active_window() works
    GTEST_SKIP() << "set_active_window not yet implemented (Agent A)";
}

TEST(WindowContextPhase1, SingleWindowUnchanged)
{
    // After Agent A refactor, single window must still work identically
    App app({.headless = true});
    auto& fig = app.figure({.width = 640, .height = 480});
    auto& ax = fig.subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 0.5f};
    ax.line(x, y);
    app.run();

    std::vector<uint8_t> pixels(640 * 480 * 4);
    ASSERT_TRUE(app.backend()->readback_framebuffer(pixels.data(), 640, 480));
}

TEST(WindowContextPhase1, GlfwTerminateNotCalledOnShutdown)
{
    // Verify GlfwAdapter::shutdown() no longer calls glfwTerminate()
    // This is a behavioral test — hard to verify without mocking.
    // For now, just verify that creating and destroying multiple Apps
    // in sequence doesn't crash (which it would if glfwTerminate was
    // called prematurely).
    for (int i = 0; i < 3; ++i)
    {
        App app({.headless = true});
        auto& fig = app.figure({.width = 320, .height = 240});
        fig.subplot(1, 1, 1);
        app.run();
    }
}

#endif  // SPECTRA_HAS_WINDOW_CONTEXT

// ═══════════════════════════════════════════════════════════════════════════════
// PHASE 2 — Multi-Window Rendering (after Agent B merge)
// Tests that multiple windows can render simultaneously.
// ═══════════════════════════════════════════════════════════════════════════════

#ifdef SPECTRA_HAS_WINDOW_MANAGER

TEST_F(MultiWindowFixture, TwoWindowsRender)
{
    GTEST_SKIP() << "WindowManager not yet implemented (Agent B)";
}

TEST_F(MultiWindowFixture, ThreeWindowsRender)
{
    GTEST_SKIP() << "WindowManager not yet implemented (Agent B)";
}

TEST_F(MultiWindowFixture, ResizeOneWindowDoesNotAffectOther)
{
    GTEST_SKIP() << "WindowManager not yet implemented (Agent B)";
}

TEST_F(MultiWindowFixture, CloseOneWindowOtherContinues)
{
    GTEST_SKIP() << "WindowManager not yet implemented (Agent B)";
}

TEST_F(MultiWindowFixture, MinimizedWindowSkipsRender)
{
    GTEST_SKIP() << "WindowManager not yet implemented (Agent B)";
}

TEST_F(MultiWindowFixture, AllWindowsMinimized)
{
    GTEST_SKIP() << "WindowManager not yet implemented (Agent B)";
}

TEST_F(MultiWindowFixture, RapidResizeTorture)
{
    // 100 rapid resizes on 3 windows simultaneously
    GTEST_SKIP() << "WindowManager not yet implemented (Agent B)";
}

TEST_F(MultiWindowFixture, WindowCloseOrderPermutations)
{
    // Close windows in every permutation — no crash
    GTEST_SKIP() << "WindowManager not yet implemented (Agent B)";
}

#endif  // SPECTRA_HAS_WINDOW_MANAGER

// ═══════════════════════════════════════════════════════════════════════════════
// PHASE 2 STUB — Multi-Window Simulation (always runs)
// Uses the stub MultiWindowFixture (N independent headless Apps) to validate
// the test structure itself.
// ═══════════════════════════════════════════════════════════════════════════════

#ifndef SPECTRA_HAS_WINDOW_MANAGER

TEST_F(MultiWindowFixture, StubTwoWindowsRender)
{
    create_windows(2);
    EXPECT_EQ(active_window_count(), 2u);
    EXPECT_TRUE(render_all_windows());
}

TEST_F(MultiWindowFixture, StubThreeWindowsRender)
{
    create_windows(3);
    EXPECT_EQ(active_window_count(), 3u);
    EXPECT_TRUE(render_all_windows());
}

TEST_F(MultiWindowFixture, StubReadbackDifferentContent)
{
    create_windows(2);
    render_all_windows();

    std::vector<uint8_t> pixels0, pixels1;
    ASSERT_TRUE(readback_window(0, pixels0));
    ASSERT_TRUE(readback_window(1, pixels1));

    // Both should have non-zero content
    bool has0 = false, has1 = false;
    for (auto p : pixels0)
        if (p != 0) { has0 = true; break; }
    for (auto p : pixels1)
        if (p != 0) { has1 = true; break; }
    EXPECT_TRUE(has0);
    EXPECT_TRUE(has1);
}

TEST_F(MultiWindowFixture, StubNoHangMultipleWindows)
{
    create_windows(3);
    GpuHangDetector detector(std::chrono::seconds(30));
    detector.expect_no_hang("render 3 stub windows", [&]() {
        render_all_windows();
    });
}

#endif  // !SPECTRA_HAS_WINDOW_MANAGER

// ═══════════════════════════════════════════════════════════════════════════════
// PHASE 3 — Figure Ownership (after Agent C merge)
// Tests that figures have stable IDs and can move between windows.
// ═══════════════════════════════════════════════════════════════════════════════

#ifdef SPECTRA_HAS_FIGURE_REGISTRY

TEST(FigureOwnership, StableIds)
{
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

TEST(FigureOwnership, MoveFigureBetweenWindows)
{
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

TEST(FigureOwnership, GpuBuffersSurviveMove)
{
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

TEST(FigureOwnership, CloseSourceWindowAfterMove)
{
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

TEST(FigureOwnership, AnimationCallbacksAfterMove)
{
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

#endif  // SPECTRA_HAS_FIGURE_REGISTRY

// ═══════════════════════════════════════════════════════════════════════════════
// PHASE 4 — Tab Tear-Off (after Agent D merge)
// Tests for drag-to-detach UX.
// ═══════════════════════════════════════════════════════════════════════════════

#ifdef SPECTRA_HAS_TEAR_OFF

TEST(TearOff, DetachCreatesNewWindow)
{
    GTEST_SKIP() << "Tear-off not yet implemented (Agent D)";
}

TEST(TearOff, DetachLastFigureBlocked)
{
    GTEST_SKIP() << "Tear-off not yet implemented (Agent D)";
}

TEST(TearOff, RapidDetachStress)
{
    GTEST_SKIP() << "Tear-off not yet implemented (Agent D)";
}

TEST(TearOff, ResizeAfterDetach)
{
    GTEST_SKIP() << "Tear-off not yet implemented (Agent D)";
}

#endif  // SPECTRA_HAS_TEAR_OFF

// ═══════════════════════════════════════════════════════════════════════════════
// Utility Tests — verify test infrastructure itself
// ═══════════════════════════════════════════════════════════════════════════════

TEST(TestInfrastructure, GpuHangDetectorCompletes)
{
    GpuHangDetector detector(std::chrono::seconds(5));
    bool ok = detector.run("trivial", []() {
        // Instant completion
    });
    EXPECT_TRUE(ok);
    EXPECT_TRUE(detector.completed());
    EXPECT_FALSE(detector.timed_out());
    EXPECT_GE(detector.elapsed_ms(), 0);
}

TEST(TestInfrastructure, GpuHangDetectorTimeout)
{
    GpuHangDetector detector(std::chrono::milliseconds(50));
    bool ok = detector.run("intentional hang", []() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    });
    // The callable still completes (we can't kill threads), but the
    // detector reports timeout
    EXPECT_FALSE(ok);
    EXPECT_TRUE(detector.timed_out());
    EXPECT_FALSE(detector.failure_reason().empty());
}

TEST(TestInfrastructure, TimingMeasure)
{
    double ms = measure_ms([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });
    EXPECT_GE(ms, 5.0);   // At least 5ms (allowing for scheduling jitter)
    EXPECT_LT(ms, 500.0); // Not absurdly long
}

TEST(TestInfrastructure, StressRunner)
{
    int counter = 0;
    auto stats = run_stress(10, [&]() {
        ++counter;
    });
    EXPECT_EQ(counter, 10);
    EXPECT_EQ(stats.iterations, 10u);
    EXPECT_GE(stats.min_ms, 0.0);
    EXPECT_GE(stats.max_ms, stats.min_ms);
    EXPECT_GE(stats.avg_ms, 0.0);
}
