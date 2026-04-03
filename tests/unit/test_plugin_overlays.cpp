// test_plugin_overlays.cpp — Unit tests for overlay plugin registration and draw_all dispatch.

#include <gtest/gtest.h>

#include "ui/commands/command_registry.hpp"
#include "ui/commands/shortcut_manager.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/overlay/overlay_registry.hpp"
#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

// ─── Direct C ABI overlay tests (no plugin loading) ──────────────────────────

TEST(PluginOverlay, RegisterAndDrawAll)
{
    OverlayRegistry reg;

    int call_count = 0;

    // Register via C++ API wrapping what the C ABI does internally
    reg.register_overlay("TestOverlay", [&](const OverlayDrawContext& ctx) { ++call_count; });

    EXPECT_EQ(reg.count(), 1u);

    auto names = reg.overlay_names();
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "TestOverlay");

    // Build a context with known viewport values
    OverlayDrawContext ctx{};
    ctx.viewport_x   = 100.0f;
    ctx.viewport_y   = 50.0f;
    ctx.viewport_w   = 800.0f;
    ctx.viewport_h   = 600.0f;
    ctx.mouse_x      = 400.0f;
    ctx.mouse_y      = 300.0f;
    ctx.is_hovered   = true;
    ctx.figure_id    = 1;
    ctx.axes_index   = 0;
    ctx.series_count = 3;
    ctx.draw_list    = nullptr;

    reg.draw_all(ctx);
    EXPECT_EQ(call_count, 1);

    reg.draw_all(ctx);
    EXPECT_EQ(call_count, 2);
}

TEST(PluginOverlay, DrawAllPassesCorrectViewport)
{
    OverlayRegistry reg;

    float captured_vx = 0, captured_vy = 0, captured_vw = 0, captured_vh = 0;
    float captured_mx = 0, captured_my = 0;
    bool  captured_hovered  = false;
    int   captured_axes_idx = -1;
    int   captured_series   = -1;

    reg.register_overlay("Capture",
                         [&](const OverlayDrawContext& ctx)
                         {
                             captured_vx       = ctx.viewport_x;
                             captured_vy       = ctx.viewport_y;
                             captured_vw       = ctx.viewport_w;
                             captured_vh       = ctx.viewport_h;
                             captured_mx       = ctx.mouse_x;
                             captured_my       = ctx.mouse_y;
                             captured_hovered  = ctx.is_hovered;
                             captured_axes_idx = ctx.axes_index;
                             captured_series   = ctx.series_count;
                         });

    OverlayDrawContext ctx{};
    ctx.viewport_x   = 10.0f;
    ctx.viewport_y   = 20.0f;
    ctx.viewport_w   = 640.0f;
    ctx.viewport_h   = 480.0f;
    ctx.mouse_x      = 320.0f;
    ctx.mouse_y      = 240.0f;
    ctx.is_hovered   = true;
    ctx.figure_id    = 42;
    ctx.axes_index   = 2;
    ctx.series_count = 5;
    ctx.draw_list    = nullptr;

    reg.draw_all(ctx);

    EXPECT_FLOAT_EQ(captured_vx, 10.0f);
    EXPECT_FLOAT_EQ(captured_vy, 20.0f);
    EXPECT_FLOAT_EQ(captured_vw, 640.0f);
    EXPECT_FLOAT_EQ(captured_vh, 480.0f);
    EXPECT_FLOAT_EQ(captured_mx, 320.0f);
    EXPECT_FLOAT_EQ(captured_my, 240.0f);
    EXPECT_TRUE(captured_hovered);
    EXPECT_EQ(captured_axes_idx, 2);
    EXPECT_EQ(captured_series, 5);
}

TEST(PluginOverlay, MultipleOverlaysAllInvoked)
{
    OverlayRegistry reg;

    int count_a = 0, count_b = 0, count_c = 0;
    reg.register_overlay("A", [&](const OverlayDrawContext&) { ++count_a; });
    reg.register_overlay("B", [&](const OverlayDrawContext&) { ++count_b; });
    reg.register_overlay("C", [&](const OverlayDrawContext&) { ++count_c; });

    EXPECT_EQ(reg.count(), 3u);

    OverlayDrawContext ctx{};
    reg.draw_all(ctx);

    EXPECT_EQ(count_a, 1);
    EXPECT_EQ(count_b, 1);
    EXPECT_EQ(count_c, 1);
}

TEST(PluginOverlay, UnregisterOverlay)
{
    OverlayRegistry reg;

    int count = 0;
    reg.register_overlay("Removable", [&](const OverlayDrawContext&) { ++count; });
    EXPECT_EQ(reg.count(), 1u);

    reg.unregister_overlay("Removable");
    EXPECT_EQ(reg.count(), 0u);

    OverlayDrawContext ctx{};
    reg.draw_all(ctx);
    EXPECT_EQ(count, 0);   // Must not be called after unregistration
}

TEST(PluginOverlay, UnregisterNonExistentIsNoOp)
{
    OverlayRegistry reg;

    reg.register_overlay("Keep", [](const OverlayDrawContext&) {});
    reg.unregister_overlay("DoesNotExist");
    EXPECT_EQ(reg.count(), 1u);
}

TEST(PluginOverlay, ReRegisterReplacesCallback)
{
    OverlayRegistry reg;

    int first_count  = 0;
    int second_count = 0;

    reg.register_overlay("Same", [&](const OverlayDrawContext&) { ++first_count; });
    reg.register_overlay("Same", [&](const OverlayDrawContext&) { ++second_count; });

    EXPECT_EQ(reg.count(), 1u);   // Should replace, not duplicate

    OverlayDrawContext ctx{};
    reg.draw_all(ctx);
    EXPECT_EQ(first_count, 0);
    EXPECT_EQ(second_count, 1);
}

// ─── C ABI function tests ────────────────────────────────────────────────────

// Tracks what the C overlay callback received
struct COverlayCapture
{
    int   call_count = 0;
    float vx = 0, vy = 0, vw = 0, vh = 0;
    float mx = 0, my = 0;
    int   hovered    = 0;
    int   axes_index = -1;
    int   series     = -1;
};

static void c_overlay_callback(const SpectraOverlayContext* ctx, void* user_data)
{
    auto* cap = static_cast<COverlayCapture*>(user_data);
    cap->call_count++;
    cap->vx         = ctx->viewport_x;
    cap->vy         = ctx->viewport_y;
    cap->vw         = ctx->viewport_w;
    cap->vh         = ctx->viewport_h;
    cap->mx         = ctx->mouse_x;
    cap->my         = ctx->mouse_y;
    cap->hovered    = ctx->is_hovered;
    cap->axes_index = ctx->axes_index;
    cap->series     = ctx->series_count;
}

TEST(PluginOverlayCAPI, RegisterViaCABI)
{
    OverlayRegistry reg;
    COverlayCapture cap;

    auto* handle = static_cast<SpectraOverlayRegistry>(&reg);
    int   ret    = spectra_register_overlay(handle, "CAPI_Overlay", c_overlay_callback, &cap);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(reg.count(), 1u);

    auto names = reg.overlay_names();
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "CAPI_Overlay");

    // Invoke draw_all and verify the C callback receives correct data
    OverlayDrawContext ctx{};
    ctx.viewport_x   = 10.0f;
    ctx.viewport_y   = 20.0f;
    ctx.viewport_w   = 800.0f;
    ctx.viewport_h   = 600.0f;
    ctx.mouse_x      = 400.0f;
    ctx.mouse_y      = 300.0f;
    ctx.is_hovered   = true;
    ctx.figure_id    = 7;
    ctx.axes_index   = 1;
    ctx.series_count = 4;
    ctx.draw_list    = nullptr;

    reg.draw_all(ctx);

    EXPECT_EQ(cap.call_count, 1);
    EXPECT_FLOAT_EQ(cap.vx, 10.0f);
    EXPECT_FLOAT_EQ(cap.vy, 20.0f);
    EXPECT_FLOAT_EQ(cap.vw, 800.0f);
    EXPECT_FLOAT_EQ(cap.vh, 600.0f);
    EXPECT_FLOAT_EQ(cap.mx, 400.0f);
    EXPECT_FLOAT_EQ(cap.my, 300.0f);
    EXPECT_EQ(cap.hovered, 1);
    EXPECT_EQ(cap.axes_index, 1);
    EXPECT_EQ(cap.series, 4);
}

TEST(PluginOverlayCAPI, UnregisterViaCABI)
{
    OverlayRegistry reg;

    auto* handle = static_cast<SpectraOverlayRegistry>(&reg);
    int   ret    = spectra_register_overlay(handle, "ToRemove", c_overlay_callback, nullptr);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(reg.count(), 1u);

    ret = spectra_unregister_overlay(handle, "ToRemove");
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(reg.count(), 0u);
}

TEST(PluginOverlayCAPI, RegisterNullRegistryFails)
{
    int ret = spectra_register_overlay(nullptr, "X", c_overlay_callback, nullptr);
    EXPECT_NE(ret, 0);
}

TEST(PluginOverlayCAPI, RegisterNullNameFails)
{
    OverlayRegistry reg;
    auto*           handle = static_cast<SpectraOverlayRegistry>(&reg);
    int             ret    = spectra_register_overlay(handle, nullptr, c_overlay_callback, nullptr);
    EXPECT_NE(ret, 0);
    EXPECT_EQ(reg.count(), 0u);
}

TEST(PluginOverlayCAPI, RegisterNullCallbackFails)
{
    OverlayRegistry reg;
    auto*           handle = static_cast<SpectraOverlayRegistry>(&reg);
    int             ret    = spectra_register_overlay(handle, "NoCallback", nullptr, nullptr);
    EXPECT_NE(ret, 0);
    EXPECT_EQ(reg.count(), 0u);
}

// ─── Plugin loading tests (requires mock_overlay_plugin.so) ──────────────────

#ifdef SPECTRA_MOCK_OVERLAY_PLUGIN_PATH

    #include "ui/workspace/plugin_api.hpp"

class PluginOverlayLoadTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        plugin_path_ = SPECTRA_MOCK_OVERLAY_PLUGIN_PATH;

        mgr_.set_command_registry(&cmd_reg_);
        mgr_.set_shortcut_manager(&shortcut_mgr_);
        mgr_.set_undo_manager(&undo_mgr_);
        mgr_.set_overlay_registry(&overlay_reg_);
    }

    // Destroy overlay registry after plugin manager so the .so's callbacks
    // are still valid while mgr_ unloads plugins.
    std::string     plugin_path_;
    CommandRegistry cmd_reg_;
    ShortcutManager shortcut_mgr_;
    UndoManager     undo_mgr_;
    OverlayRegistry overlay_reg_;
    PluginManager   mgr_;
};

TEST_F(PluginOverlayLoadTest, LoadPluginRegistersOverlay)
{
    ASSERT_TRUE(mgr_.load_plugin(plugin_path_));
    EXPECT_EQ(mgr_.plugin_count(), 1u);

    auto names = overlay_reg_.overlay_names();
    EXPECT_FALSE(names.empty());

    // The mock plugin registers "MockCrosshair"
    bool found = false;
    for (const auto& n : names)
    {
        if (n == "MockCrosshair")
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected overlay 'MockCrosshair' not found";
}

TEST_F(PluginOverlayLoadTest, OverlayCallbackInvokedOnDrawAll)
{
    ASSERT_TRUE(mgr_.load_plugin(plugin_path_));

    OverlayDrawContext ctx{};
    ctx.viewport_x   = 50.0f;
    ctx.viewport_y   = 60.0f;
    ctx.viewport_w   = 1024.0f;
    ctx.viewport_h   = 768.0f;
    ctx.mouse_x      = 512.0f;
    ctx.mouse_y      = 384.0f;
    ctx.is_hovered   = true;
    ctx.figure_id    = 1;
    ctx.axes_index   = 0;
    ctx.series_count = 2;
    ctx.draw_list    = nullptr;

    // Should not crash — the mock callback is a no-op draw
    EXPECT_NO_THROW(overlay_reg_.draw_all(ctx));
}

TEST_F(PluginOverlayLoadTest, UnloadRemovesOverlay)
{
    ASSERT_TRUE(mgr_.load_plugin(plugin_path_));
    EXPECT_GE(overlay_reg_.count(), 1u);

    // Get the plugin name from the loaded entry
    auto plugins = mgr_.plugins();
    ASSERT_FALSE(plugins.empty());
    mgr_.unload_plugin(plugins[0].name);

    // After unload, overlay should be gone
    auto names = overlay_reg_.overlay_names();
    for (const auto& n : names)
    {
        EXPECT_NE(n, "MockCrosshair") << "Overlay 'MockCrosshair' still registered after unload";
    }
}

#endif   // SPECTRA_MOCK_OVERLAY_PLUGIN_PATH
