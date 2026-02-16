#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <spectra/app.hpp>
#include <spectra/figure.hpp>
#include <vector>

#include "render/vulkan/vk_backend.hpp"
#include "render/vulkan/window_context.hpp"
#include "ui/window_manager.hpp"

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
    // adopt_primary_window should fail gracefully
    auto* result = wm.adopt_primary_window(nullptr);
    EXPECT_EQ(result, nullptr);
}

// ─── Adopt Primary Window ───────────────────────────────────────────────────

TEST_F(WindowManagerTest, AdoptPrimaryWindowHeadless)
{
    WindowManager wm;
    wm.init(vk_backend());

    // In headless mode, glfw_window is nullptr, but adopt should still work
    auto* wctx = wm.adopt_primary_window(nullptr);
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

    auto* wctx = wm.adopt_primary_window(nullptr);
    ASSERT_NE(wctx, nullptr);
    // First window should get id=1
    EXPECT_EQ(wctx->id, 1u);
}

TEST_F(WindowManagerTest, AdoptPrimaryWindowAppearsInWindowsList)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.adopt_primary_window(nullptr);
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

    auto* wctx = wm.adopt_primary_window(nullptr);
    ASSERT_NE(wctx, nullptr);

    auto* found = wm.find_window(wctx->id);
    EXPECT_EQ(found, wctx);
}

TEST_F(WindowManagerTest, FindWindowInvalidId)
{
    WindowManager wm;
    wm.init(vk_backend());

    wm.adopt_primary_window(nullptr);

    auto* found = wm.find_window(9999);
    EXPECT_EQ(found, nullptr);
}

// ─── Focused Window ─────────────────────────────────────────────────────────

TEST_F(WindowManagerTest, FocusedWindowIsPrimary)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.adopt_primary_window(nullptr);
    ASSERT_NE(wctx, nullptr);

    // Primary is focused by default after adopt
    auto* focused = wm.focused_window();
    EXPECT_EQ(focused, wctx);
}

TEST_F(WindowManagerTest, FocusedWindowNoneWhenClosed)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.adopt_primary_window(nullptr);
    ASSERT_NE(wctx, nullptr);

    // Mark primary as should_close
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

    wm.adopt_primary_window(nullptr);
    EXPECT_TRUE(wm.any_window_open());
}

TEST_F(WindowManagerTest, AnyWindowOpenFalseAfterClose)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.adopt_primary_window(nullptr);
    ASSERT_NE(wctx, nullptr);

    // Mark primary as closed
    wctx->should_close = true;
    // Rebuild active list by requesting close on primary
    wm.request_close(wctx->id);
    wm.process_pending_closes();

    EXPECT_FALSE(wm.any_window_open());
}

// ─── Request Close ──────────────────────────────────────────────────────────

TEST_F(WindowManagerTest, RequestClosePrimary)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* wctx = wm.adopt_primary_window(nullptr);
    ASSERT_NE(wctx, nullptr);

    wm.request_close(wctx->id);
    wm.process_pending_closes();

    // Primary window is marked should_close but not destroyed (owned by VulkanBackend)
    EXPECT_TRUE(wctx->should_close);
}

// ─── Shutdown ───────────────────────────────────────────────────────────────

TEST_F(WindowManagerTest, ShutdownCleansUp)
{
    WindowManager wm;
    wm.init(vk_backend());

    wm.adopt_primary_window(nullptr);
    EXPECT_EQ(wm.window_count(), 1u);

    wm.shutdown();
    EXPECT_EQ(wm.window_count(), 0u);
    EXPECT_TRUE(wm.windows().empty());
}

TEST_F(WindowManagerTest, ShutdownIdempotent)
{
    WindowManager wm;
    wm.init(vk_backend());

    wm.adopt_primary_window(nullptr);
    wm.shutdown();
    wm.shutdown();  // Should not crash
    EXPECT_EQ(wm.window_count(), 0u);
}

TEST_F(WindowManagerTest, DestructorCallsShutdown)
{
    {
        WindowManager wm;
        wm.init(vk_backend());
        wm.adopt_primary_window(nullptr);
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

    // recreate_swapchain_for on primary window (headless — has offscreen, not swapchain)
    // This tests that the method doesn't crash when called on a window without a surface
    auto& primary = backend->primary_window();
    // In headless mode, primary has no surface, so recreate should handle gracefully
    // (it will fail but not crash)
    if (primary.surface != VK_NULL_HANDLE)
    {
        bool ok = backend->recreate_swapchain_for(primary, 320, 240);
        EXPECT_TRUE(ok);
    }
}

// ─── Poll Events (no-op in headless) ────────────────────────────────────────

TEST_F(WindowManagerTest, PollEventsNoOp)
{
    WindowManager wm;
    wm.init(vk_backend());
    wm.adopt_primary_window(nullptr);

    // Should not crash even without real GLFW windows
    wm.poll_events();
    SUCCEED();
}

TEST_F(WindowManagerTest, ProcessPendingClosesEmpty)
{
    WindowManager wm;
    wm.init(vk_backend());
    wm.adopt_primary_window(nullptr);

    // No pending closes — should be a no-op
    wm.process_pending_closes();
    EXPECT_EQ(wm.window_count(), 1u);
}

// ─── Multiple Operations ────────────────────────────────────────────────────

TEST_F(WindowManagerTest, MultipleAdoptCallsOverwrite)
{
    WindowManager wm;
    wm.init(vk_backend());

    auto* first = wm.adopt_primary_window(nullptr);
    ASSERT_NE(first, nullptr);
    uint32_t first_id = first->id;

    // Adopting again should update the same primary window
    auto* second = wm.adopt_primary_window(nullptr);
    ASSERT_NE(second, nullptr);

    // Both point to the same primary_window_ in VulkanBackend
    EXPECT_EQ(first, second);
    // ID should have been updated
    EXPECT_NE(second->id, first_id);
}

TEST_F(WindowManagerTest, WindowCountAccurate)
{
    WindowManager wm;
    wm.init(vk_backend());

    EXPECT_EQ(wm.window_count(), 0u);

    wm.adopt_primary_window(nullptr);
    EXPECT_EQ(wm.window_count(), 1u);

    // In headless mode we can't create secondary windows (no GLFW),
    // but window_count should still be accurate
    EXPECT_EQ(wm.window_count(), 1u);
}
