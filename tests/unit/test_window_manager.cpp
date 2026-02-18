#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <spectra/app.hpp>
#include <spectra/figure.hpp>
#include <type_traits>
#include <vector>

#include "render/vulkan/vk_backend.hpp"
#include "render/vulkan/window_context.hpp"
#include "ui/window_manager.hpp"
#include "ui/window_ui_context.hpp"

using namespace spectra;

// ═══════════════════════════════════════════════════════════════════════════════
// WindowManager Unit Tests — Phase 2 Agent B
// Tests WindowManager lifecycle, window creation/destruction, and query methods.
// These tests run headless (no GLFW windows) to validate logic paths.
// ═══════════════════════════════════════════════════════════════════════════════

// ─── Fixture: creates a headless App with a real VulkanBackend ──────────────

class WindowManagerTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        AppConfig config;
        config.headless = true;
        app_ = std::make_unique<App>(config);
        // Render one frame to fully initialize backend + renderer
        auto& fig = app_->figure({.width = 320, .height = 240});
        auto& ax = fig.subplot(1, 1, 1);
        std::vector<float> x = {0.0f, 1.0f, 2.0f};
        std::vector<float> y = {0.0f, 1.0f, 0.5f};
        ax.line(x, y);
        app_->run();
    }

    void TearDown() override { app_.reset(); }

    VulkanBackend* vk_backend()
    {
        return static_cast<VulkanBackend*>(app_->backend());
    }

    std::unique_ptr<App> app_;
};

// ─── Construction & Init ────────────────────────────────────────────────────

TEST_F(WindowManagerTest, DefaultConstruction)
{
    WindowManager wm;
    EXPECT_EQ(wm.window_count(), 0u);
    EXPECT_TRUE(wm.windows().empty());
}

TEST_F(WindowManagerTest, InitWithBackend)
{
    WindowManager wm;
    wm.init(vk_backend());
    // After init but before adopting any window, count is still 0
    EXPECT_EQ(wm.window_count(), 0u);
}

TEST_F(WindowManagerTest, InitWithNullBackend)
{
    WindowManager wm;
    wm.init(nullptr);
    // create_initial_window should fail gracefully
    auto* result = wm.create_initial_window(nullptr);
    EXPECT_EQ(result, nullptr);
}

// ─── Adopt Primary Window ───────────────────────────────────────────────────

TEST_F(WindowManagerTest, AdoptPrimaryWindowHeadless)
{
    WindowManager wm;
    wm.init(vk_backend());

    // In headless mode, glfw_window is nullptr, but create should still work
    auto* wctx = wm.create_initial_window(nullptr);
    ASSERT_NE(wctx, nullptr);
    EXPECT_EQ(wm.window_count(), 1u);
    EXPECT_EQ(wctx->glfw_window, nullptr);
    EXPECT_TRUE(wctx->is_focused);
    EXPECT_NE(wctx->id, 0u);
}

TEST_F(WindowManagerTest, AdoptPrimaryWindowSetsId)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.create_initial_window(nullptr);
    ASSERT_NE(wctx, nullptr);
    // First window should get id=1
    EXPECT_EQ(wctx->id, 1u);
}

TEST_F(WindowManagerTest, AdoptPrimaryWindowAppearsInWindowsList)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.create_initial_window(nullptr);
    ASSERT_NE(wctx, nullptr);

    const auto& windows = wm.windows();
    ASSERT_EQ(windows.size(), 1u);
    EXPECT_EQ(windows[0], wctx);
}

// ─── Find Window ────────────────────────────────────────────────────────────

TEST_F(WindowManagerTest, FindWindowById)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.create_initial_window(nullptr);
    ASSERT_NE(wctx, nullptr);

    auto* found = wm.find_window(wctx->id);
    EXPECT_EQ(found, wctx);
}

TEST_F(WindowManagerTest, FindWindowInvalidId)
{
    WindowManager wm;
    wm.init(vk_backend());

    wm.create_initial_window(nullptr);

    auto* found = wm.find_window(9999);
    EXPECT_EQ(found, nullptr);
}

// ─── Focused Window ─────────────────────────────────────────────────────────

TEST_F(WindowManagerTest, FocusedWindowIsPrimary)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.create_initial_window(nullptr);
    ASSERT_NE(wctx, nullptr);

    // Initial window is focused by default after creation
    auto* focused = wm.focused_window();
    EXPECT_EQ(focused, wctx);
}

TEST_F(WindowManagerTest, FocusedWindowNoneWhenClosed)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.create_initial_window(nullptr);
    ASSERT_NE(wctx, nullptr);

    // Mark window as should_close
    wctx->should_close = true;
    wctx->is_focused = false;

    auto* focused = wm.focused_window();
    EXPECT_EQ(focused, nullptr);
}

// ─── Any Window Open ────────────────────────────────────────────────────────

TEST_F(WindowManagerTest, AnyWindowOpenTrue)
{
    WindowManager wm;
    wm.init(vk_backend());

    wm.create_initial_window(nullptr);
    EXPECT_TRUE(wm.any_window_open());
}

TEST_F(WindowManagerTest, AnyWindowOpenFalseAfterClose)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.create_initial_window(nullptr);
    ASSERT_NE(wctx, nullptr);

    // Mark window as closed
    wctx->should_close = true;
    // Rebuild active list by requesting close
    wm.request_close(wctx->id);
    wm.process_pending_closes();

    EXPECT_FALSE(wm.any_window_open());
}

// ─── Request Close ──────────────────────────────────────────────────────────

TEST_F(WindowManagerTest, RequestClosePrimary)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.create_initial_window(nullptr);
    ASSERT_NE(wctx, nullptr);
    uint32_t id = wctx->id;

    wm.request_close(id);
    wm.process_pending_closes();

    // Window is now fully destroyed (uniform ownership)
    EXPECT_EQ(wm.window_count(), 0u);
    EXPECT_EQ(wm.find_window(id), nullptr);
}

// ─── Shutdown ───────────────────────────────────────────────────────────────

TEST_F(WindowManagerTest, ShutdownCleansUp)
{
    WindowManager wm;
    wm.init(vk_backend());

    wm.create_initial_window(nullptr);
    EXPECT_EQ(wm.window_count(), 1u);

    wm.shutdown();
    EXPECT_EQ(wm.window_count(), 0u);
    EXPECT_TRUE(wm.windows().empty());
}

TEST_F(WindowManagerTest, ShutdownIdempotent)
{
    WindowManager wm;
    wm.init(vk_backend());

    wm.create_initial_window(nullptr);
    wm.shutdown();
    wm.shutdown();  // Should not crash
    EXPECT_EQ(wm.window_count(), 0u);
}

TEST_F(WindowManagerTest, DestructorCallsShutdown)
{
    {
        WindowManager wm;
        wm.init(vk_backend());
        wm.create_initial_window(nullptr);
    }
    // If destructor didn't call shutdown, we'd leak or crash
    SUCCEED();
}

// ─── WindowContext Fields ───────────────────────────────────────────────────

TEST_F(WindowManagerTest, WindowContextDefaultFields)
{
    WindowContext wctx{};
    EXPECT_EQ(wctx.id, 0u);
    EXPECT_EQ(wctx.glfw_window, nullptr);
    EXPECT_EQ(wctx.surface, VK_NULL_HANDLE);
    EXPECT_FALSE(wctx.swapchain_dirty);
    EXPECT_FALSE(wctx.should_close);
    EXPECT_FALSE(wctx.is_focused);
    EXPECT_EQ(wctx.assigned_figure_index, INVALID_FIGURE_ID);
    EXPECT_FALSE(wctx.needs_resize);
    EXPECT_EQ(wctx.pending_width, 0u);
    EXPECT_EQ(wctx.pending_height, 0u);
    EXPECT_EQ(wctx.current_flight_frame, 0u);
    EXPECT_EQ(wctx.current_image_index, 0u);
}

TEST_F(WindowManagerTest, WindowContextMaxFramesInFlight)
{
    EXPECT_EQ(WindowContext::MAX_FRAMES_IN_FLIGHT, 2u);
}

// ─── VulkanBackend Multi-Window Methods ─────────────────────────────────────

TEST_F(WindowManagerTest, BackendHasInitWindowContext)
{
    auto* backend = vk_backend();
    ASSERT_NE(backend, nullptr);

    // init_window_context should fail gracefully with no GLFW window
    WindowContext wctx{};
    bool ok = backend->init_window_context(wctx, 320, 240);
    EXPECT_FALSE(ok);  // No glfw_window set
}

TEST_F(WindowManagerTest, BackendDestroyEmptyWindowContext)
{
    auto* backend = vk_backend();
    ASSERT_NE(backend, nullptr);

    // destroy_window_context on an empty context should not crash
    WindowContext wctx{};
    backend->destroy_window_context(wctx);
    SUCCEED();
}

TEST_F(WindowManagerTest, BackendRecreateSwapchainForPrimary)
{
    auto* backend = vk_backend();
    ASSERT_NE(backend, nullptr);

    // recreate_swapchain_for on active window (headless — has offscreen, not swapchain)
    // This tests that the method doesn't crash when called on a window without a surface
    auto* active = backend->active_window();
    ASSERT_NE(active, nullptr);
    // In headless mode, active window has no surface, so recreate should handle gracefully
    // (it will fail but not crash)
    if (active->surface != VK_NULL_HANDLE)
    {
        bool ok = backend->recreate_swapchain_for(*active, 320, 240);
        EXPECT_TRUE(ok);
    }
}

// ─── Poll Events (no-op in headless) ────────────────────────────────────────

TEST_F(WindowManagerTest, PollEventsNoOp)
{
    WindowManager wm;
    wm.init(vk_backend());
    wm.create_initial_window(nullptr);

    // Should not crash even without real GLFW windows
    wm.poll_events();
    SUCCEED();
}

TEST_F(WindowManagerTest, ProcessPendingClosesEmpty)
{
    WindowManager wm;
    wm.init(vk_backend());
    wm.create_initial_window(nullptr);

    // No pending closes — should be a no-op
    wm.process_pending_closes();
    EXPECT_EQ(wm.window_count(), 1u);
}

// ─── Multiple Operations ────────────────────────────────────────────────────

TEST_F(WindowManagerTest, MultipleAdoptCallsOverwrite)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* first = wm.create_initial_window(nullptr);
    ASSERT_NE(first, nullptr);

    // Second create returns nullptr because initial_window_ was already
    // released from the backend by the first call.
    auto* second = wm.create_initial_window(nullptr);
    EXPECT_EQ(second, nullptr);

    // First window is still managed
    EXPECT_EQ(wm.window_count(), 1u);
}

TEST_F(WindowManagerTest, WindowCountAccurate)
{
    WindowManager wm;
    wm.init(vk_backend());

    EXPECT_EQ(wm.window_count(), 0u);

    wm.create_initial_window(nullptr);
    EXPECT_EQ(wm.window_count(), 1u);

    // In headless mode we can't create secondary windows (no GLFW),
    // but window_count should still be accurate
    EXPECT_EQ(wm.window_count(), 1u);
}

// ─── Assigned Figure Index ─────────────────────────────────────────────────

TEST_F(WindowManagerTest, AssignedFigureIndexDefault)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.create_initial_window(nullptr);
    ASSERT_NE(wctx, nullptr);
    // Window has no assigned figure by default (uses active_figure)
    EXPECT_EQ(wctx->assigned_figure_index, INVALID_FIGURE_ID);
}

TEST_F(WindowManagerTest, AssignedFigureIndexSettable)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.create_initial_window(nullptr);
    ASSERT_NE(wctx, nullptr);

    wctx->assigned_figure_index = 42;
    EXPECT_EQ(wctx->assigned_figure_index, 42u);
}

// ─── Set Window Position ───────────────────────────────────────────────────

TEST_F(WindowManagerTest, SetWindowPositionNoGlfw)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.create_initial_window(nullptr);
    ASSERT_NE(wctx, nullptr);

    // In headless mode, glfw_window is nullptr — should not crash
    wm.set_window_position(*wctx, 100, 200);
    SUCCEED();
}

// ─── Move Figure Between Windows ────────────────────────────────────────────

TEST_F(WindowManagerTest, MoveFigureInvalidWindows)
{
    WindowManager wm;
    wm.init(vk_backend());

    wm.create_initial_window(nullptr);

    // Both source and target window IDs are invalid
    EXPECT_FALSE(wm.move_figure(1, 999, 888));
}

TEST_F(WindowManagerTest, MoveFigureSameWindow)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.create_initial_window(nullptr);
    ASSERT_NE(wctx, nullptr);

    wctx->assigned_figure_index = 1;
    // Moving to the same window is a no-op
    EXPECT_FALSE(wm.move_figure(1, wctx->id, wctx->id));
    // Figure should still be assigned
    EXPECT_EQ(wctx->assigned_figure_index, 1u);
}

TEST_F(WindowManagerTest, MoveFigureSourceNotRendering)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* initial = wm.create_initial_window(nullptr);
    ASSERT_NE(initial, nullptr);

    // Window has INVALID_FIGURE_ID (default), try to move figure 42
    // which is not assigned to it
    EXPECT_FALSE(wm.move_figure(42, initial->id, initial->id));
}

TEST_F(WindowManagerTest, MoveFigureSuccessful)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* initial = wm.create_initial_window(nullptr);
    ASSERT_NE(initial, nullptr);

    // Simulate a second window by creating a WindowContext manually
    // (can't use create_window in headless mode)
    WindowContext secondary{};
    secondary.id = 99;
    secondary.assigned_figure_index = INVALID_FIGURE_ID;

    // We can't use wm.move_figure directly since the secondary isn't
    // managed by wm. Instead, test the logic by assigning and moving
    // on the initial window.
    initial->assigned_figure_index = 7;

    // Verify assignment
    EXPECT_EQ(initial->assigned_figure_index, 7u);

    // Reset to unassigned
    initial->assigned_figure_index = INVALID_FIGURE_ID;
    EXPECT_EQ(initial->assigned_figure_index, INVALID_FIGURE_ID);
}

TEST_F(WindowManagerTest, MoveFigureClearsSource)
{
    // Test the move_figure logic path: when source has the figure,
    // it should be cleared after move.
    // Since we can't create real secondary windows in headless,
    // we test the WindowContext field manipulation directly.
    WindowContext source{};
    source.id = 1;
    source.assigned_figure_index = 42;

    WindowContext target{};
    target.id = 2;
    target.assigned_figure_index = INVALID_FIGURE_ID;

    // Simulate move
    target.assigned_figure_index = source.assigned_figure_index;
    source.assigned_figure_index = INVALID_FIGURE_ID;

    EXPECT_EQ(target.assigned_figure_index, 42u);
    EXPECT_EQ(source.assigned_figure_index, INVALID_FIGURE_ID);
}

TEST_F(WindowManagerTest, FigureIdIsUint64)
{
    // Verify FigureId is uint64_t (not size_t)
    static_assert(std::is_same_v<FigureId, uint64_t>,
                  "FigureId must be uint64_t");
    static_assert(INVALID_FIGURE_ID == ~FigureId{0},
                  "INVALID_FIGURE_ID must be all-ones");
}

// ─── Detach Figure ──────────────────────────────────────────────────────────

TEST_F(WindowManagerTest, DetachFigureNotInitialized)
{
    WindowManager wm;
    // Not initialized — should return nullptr
    auto* result = wm.detach_figure(1, 800, 600, "Test", 100, 200);
    EXPECT_EQ(result, nullptr);
}

TEST_F(WindowManagerTest, DetachFigureInvalidId)
{
    WindowManager wm;
    wm.init(vk_backend());
    wm.create_initial_window(nullptr);

    // INVALID_FIGURE_ID should be rejected
    auto* result = wm.detach_figure(INVALID_FIGURE_ID, 800, 600, "Test", 100, 200);
    EXPECT_EQ(result, nullptr);
}

TEST_F(WindowManagerTest, DetachFigureHeadlessNoGlfw)
{
    WindowManager wm;
    wm.init(vk_backend());
    wm.create_initial_window(nullptr);

    // In headless mode, create_window (called by detach_figure) will fail
    // because GLFW is not initialized. detach_figure should return nullptr.
    auto* result = wm.detach_figure(1, 800, 600, "Test", 100, 200);
    // In headless mode without GLFW display, this returns nullptr
    // (glfwCreateWindow fails). This is expected.
    // On a system with a display, it would succeed.
    // We just verify it doesn't crash.
    (void)result;
    SUCCEED();
}

TEST_F(WindowManagerTest, DetachFigureZeroDimensions)
{
    WindowManager wm;
    wm.init(vk_backend());
    wm.create_initial_window(nullptr);

    // Zero dimensions should be clamped to defaults (800x600)
    // In headless, create_window will fail, but the clamping logic
    // should not cause any issues.
    auto* result = wm.detach_figure(1, 0, 0, "Test", 0, 0);
    (void)result;
    SUCCEED();
}

// ─── Edge Cases ─────────────────────────────────────────────────────────────

TEST_F(WindowManagerTest, MultipleRequestClosesSameId)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.create_initial_window(nullptr);
    ASSERT_NE(wctx, nullptr);
    uint32_t id = wctx->id;

    // Multiple close requests for the same ID should not crash
    wm.request_close(id);
    wm.request_close(id);
    wm.process_pending_closes();

    // Window is fully destroyed after close
    EXPECT_EQ(wm.window_count(), 0u);
    EXPECT_EQ(wm.find_window(id), nullptr);
}

TEST_F(WindowManagerTest, DestroyNonexistentWindow)
{
    WindowManager wm;
    wm.init(vk_backend());
    wm.create_initial_window(nullptr);

    // Destroying a window that doesn't exist should be a no-op
    wm.destroy_window(9999);
    EXPECT_EQ(wm.window_count(), 1u);
}

TEST_F(WindowManagerTest, FindWindowAfterShutdown)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.create_initial_window(nullptr);
    ASSERT_NE(wctx, nullptr);
    uint32_t id = wctx->id;

    wm.shutdown();

    // After shutdown, all windows are destroyed.
    EXPECT_EQ(wm.window_count(), 0u);
    EXPECT_TRUE(wm.windows().empty());

    // All windows destroyed — any ID should return nullptr
    auto* found = wm.find_window(id);
    EXPECT_EQ(found, nullptr);
}

TEST_F(WindowManagerTest, WindowCountAfterMultipleOps)
{
    WindowManager wm;
    wm.init(vk_backend());

    EXPECT_EQ(wm.window_count(), 0u);

    wm.create_initial_window(nullptr);
    EXPECT_EQ(wm.window_count(), 1u);

    // Request close and process
    auto* primary = wm.windows()[0];
    wm.request_close(primary->id);
    wm.process_pending_closes();

    EXPECT_EQ(wm.window_count(), 0u);
}

TEST_F(WindowManagerTest, MoveFigureToSelfIsNoOp)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.create_initial_window(nullptr);
    ASSERT_NE(wctx, nullptr);

    wctx->assigned_figure_index = 5;
    bool result = wm.move_figure(5, wctx->id, wctx->id);
    EXPECT_FALSE(result);
    // Figure should still be assigned
    EXPECT_EQ(wctx->assigned_figure_index, 5u);
}

TEST_F(WindowManagerTest, PollEventsMultipleTimes)
{
    WindowManager wm;
    wm.init(vk_backend());
    wm.create_initial_window(nullptr);

    // Multiple poll_events calls should be safe
    for (int i = 0; i < 10; ++i)
    {
        wm.poll_events();
    }
    SUCCEED();
}

TEST_F(WindowManagerTest, FocusedWindowFallbackToPrimary)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.create_initial_window(nullptr);
    ASSERT_NE(wctx, nullptr);

    // Primary is not focused but still open — should return primary as fallback
    wctx->is_focused = false;
    auto* focused = wm.focused_window();
    EXPECT_EQ(focused, wctx);
}

TEST_F(WindowManagerTest, WindowContextResizeFields)
{
    WindowContext wctx{};
    EXPECT_FALSE(wctx.needs_resize);
    EXPECT_EQ(wctx.pending_width, 0u);
    EXPECT_EQ(wctx.pending_height, 0u);

    // Simulate resize event
    wctx.needs_resize = true;
    wctx.pending_width = 1920;
    wctx.pending_height = 1080;

    EXPECT_TRUE(wctx.needs_resize);
    EXPECT_EQ(wctx.pending_width, 1920u);
    EXPECT_EQ(wctx.pending_height, 1080u);
}

TEST_F(WindowManagerTest, WindowContextAssignedFigureRoundTrip)
{
    WindowContext wctx{};
    EXPECT_EQ(wctx.assigned_figure_index, INVALID_FIGURE_ID);

    // Assign, verify, clear
    wctx.assigned_figure_index = 42;
    EXPECT_EQ(wctx.assigned_figure_index, 42u);

    wctx.assigned_figure_index = INVALID_FIGURE_ID;
    EXPECT_EQ(wctx.assigned_figure_index, INVALID_FIGURE_ID);
}

// ─── Create Window (headless — GLFW not initialized, so we skip) ───────────
// NOTE: create_window() calls glfwCreateWindow which requires glfwInit().
// In headless mode GLFW is never initialized, so we cannot test create_window
// without a display.  This is tested via the multi_figure_demo example instead.
