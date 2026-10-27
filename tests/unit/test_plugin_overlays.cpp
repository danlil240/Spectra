// test_plugin_overlays.cpp — Unit tests for overlay plugin registration and draw_all dispatch.

#include <gtest/gtest.h>

#include <cstddef>

#include "ui/commands/command_registry.hpp"
#include "ui/commands/shortcut_manager.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/overlay/overlay_registry.hpp"
#include "ui/theme/theme.hpp"
#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

// ─── Direct C ABI overlay tests (no plugin loading) ──────────────────────────

TEST(PluginOverlay, RegisterAndDrawAll)
{
    OverlayRegistry reg;

    int call_count = 0;

    // Register via C++ API wrapping what the C ABI does internally
    reg.register_overlay("TestOverlay", [&](const OverlayDrawContext&) { ++call_count; });

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

// ─── Overlay colour packing (API v2.1 docs/helpers) ──────────────────────────

TEST(PluginOverlayColor, PackHelperPutsRedInLowByte)
{
    // ImGui's IM_COL32 layout: 0xAABBGGRR.
    EXPECT_EQ(SPECTRA_OVERLAY_COLOR(0x11, 0x22, 0x33, 0x44), 0x44332211u);
    EXPECT_EQ(spectra_overlay_pack_rgba(0x11, 0x22, 0x33, 0x44), 0x44332211u);

    // Opaque yellow is 0xFF00FFFF; the old example's "yellow" 0xFFFFFF00 is cyan.
    EXPECT_EQ(SPECTRA_OVERLAY_COLOR(255, 255, 0, 255), 0xFF00FFFFu);
    EXPECT_EQ(SPECTRA_OVERLAY_COLOR(0, 255, 255, 255), 0xFFFFFF00u);

    // Channels are masked to 8 bits.
    EXPECT_EQ(SPECTRA_OVERLAY_COLOR(0x1FF, 0, 0, 0), 0x000000FFu);
}

TEST(PluginOverlayColor, ScaleAlphaKeepsRgb)
{
    const uint32_t c = SPECTRA_OVERLAY_COLOR(10, 20, 30, 200);
    EXPECT_EQ(spectra_overlay_color_scale_alpha(c, 0.5f), SPECTRA_OVERLAY_COLOR(10, 20, 30, 100));
    EXPECT_EQ(spectra_overlay_color_scale_alpha(c, 2.0f), c);
    EXPECT_EQ(spectra_overlay_color_scale_alpha(c, -1.0f), SPECTRA_OVERLAY_COLOR(10, 20, 30, 0));
}

// ─── Overlay context carries theme + scale (API v2.1) ────────────────────────

namespace
{

struct CtxCapture
{
    int                   calls = 0;
    SpectraOverlayContext ctx{};
};

void capture_ctx(const SpectraOverlayContext* ctx, void* user_data)
{
    auto* cap = static_cast<CtxCapture*>(user_data);
    ++cap->calls;
    cap->ctx = *ctx;
}

}   // namespace

TEST(PluginOverlayCAPI, ContextCarriesThemeAndScale)
{
    OverlayRegistry reg;
    CtxCapture      cap;
    ASSERT_EQ(spectra_register_overlay(static_cast<SpectraOverlayRegistry>(&reg),
                                       "ThemeProbe",
                                       capture_ctx,
                                       &cap),
              0);

    ui::ThemeColors theme{};
    theme.bg_canvas      = ui::Color(1.0f, 1.0f, 1.0f, 1.0f);
    theme.crosshair      = ui::Color(0.2f, 0.4f, 0.6f, 0.8f);
    theme.text_primary   = ui::Color(0.0f, 0.0f, 0.0f, 1.0f);
    theme.text_secondary = ui::Color(0.25f, 0.25f, 0.25f, 1.0f);
    theme.tooltip_bg     = ui::Color(1.0f, 1.0f, 0.0f, 1.0f);
    theme.tooltip_border = ui::Color(0.0f, 1.0f, 0.0f, 0.5f);
    theme.accent         = ui::Color(1.0f, 0.0f, 0.0f, 1.0f);
    theme.axis_line      = ui::Color(0.0f, 0.0f, 1.0f, 1.0f);
    theme.grid_major     = ui::Color(0.5f, 0.5f, 0.5f, 0.25f);

    OverlayDrawContext ctx{};
    ctx.viewport_w = 100.0f;
    ctx.viewport_h = 100.0f;
    ctx.theme      = &theme;
    ctx.ui_scale   = 2.0f;
    ctx.font_size  = 26.0f;
    reg.draw_all(ctx);

    ASSERT_EQ(cap.calls, 1);
    const SpectraOverlayContext& c = cap.ctx;
    EXPECT_EQ(c.struct_size, sizeof(SpectraOverlayContext));
    EXPECT_TRUE(c.flags & SPECTRA_OVERLAY_FLAG_HAS_THEME);
    EXPECT_FLOAT_EQ(c.ui_scale, 2.0f);
    EXPECT_FLOAT_EQ(c.font_size, 26.0f);
    EXPECT_NE(c.measure_text, nullptr);

    EXPECT_EQ(c.theme.canvas_bg, SPECTRA_OVERLAY_COLOR(255, 255, 255, 255));
    EXPECT_EQ(c.theme.crosshair, SPECTRA_OVERLAY_COLOR(51, 102, 153, 204));
    EXPECT_EQ(c.theme.text, SPECTRA_OVERLAY_COLOR(0, 0, 0, 255));
    EXPECT_EQ(c.theme.text_secondary, SPECTRA_OVERLAY_COLOR(64, 64, 64, 255));
    EXPECT_EQ(c.theme.label_bg, SPECTRA_OVERLAY_COLOR(255, 255, 0, 255));
    EXPECT_EQ(c.theme.label_border, SPECTRA_OVERLAY_COLOR(0, 255, 0, 128));
    EXPECT_EQ(c.theme.accent, SPECTRA_OVERLAY_COLOR(255, 0, 0, 255));
    EXPECT_EQ(c.theme.axis_line, SPECTRA_OVERLAY_COLOR(0, 0, 255, 255));
    EXPECT_EQ(c.theme.grid_major, SPECTRA_OVERLAY_COLOR(128, 128, 128, 64));

    // Measuring without an ImGui frame is safe and reports zero.
    float w = -1.0f;
    float h = -1.0f;
    c.measure_text(&c, "abc", &w, &h);
    EXPECT_GE(w, 0.0f);
    EXPECT_GE(h, 0.0f);
}

TEST(PluginOverlayCAPI, ContextWithoutThemeHasNoThemeFlag)
{
    OverlayRegistry reg;
    CtxCapture      cap;
    ASSERT_EQ(spectra_register_overlay(static_cast<SpectraOverlayRegistry>(&reg),
                                       "NoThemeProbe",
                                       capture_ctx,
                                       &cap),
              0);

    OverlayDrawContext ctx{};
    ctx.ui_scale = 0.0f;   // invalid scale falls back to 100%
    reg.draw_all(ctx);

    ASSERT_EQ(cap.calls, 1);
    EXPECT_EQ(cap.ctx.struct_size, sizeof(SpectraOverlayContext));
    EXPECT_EQ(cap.ctx.flags & SPECTRA_OVERLAY_FLAG_HAS_THEME, 0u);
    EXPECT_FLOAT_EQ(cap.ctx.ui_scale, 1.0f);
}

TEST(PluginOverlayCAPI, AppearanceFieldsAppendedAfterV12Layout)
{
    // The v2.1 fields are appended: the v1.2 layout must be unchanged.
    EXPECT_EQ(offsetof(SpectraOverlayContext, viewport_x), 0u);
    EXPECT_EQ(offsetof(SpectraOverlayContext, series_count), 9 * sizeof(float));
    EXPECT_GT(offsetof(SpectraOverlayContext, struct_size),
              offsetof(SpectraOverlayContext, reserved_));
    EXPECT_GE(SPECTRA_PLUGIN_API_VERSION_MINOR, 1u);
}

// ─── Label placement keeps labels inside the viewport ───────────────────────

TEST(PluginOverlayLabel, PrefersUpRightOfAnchor)
{
    SpectraOverlayContext ctx{};
    ctx.viewport_x = 100.0f;
    ctx.viewport_y = 50.0f;
    ctx.viewport_w = 400.0f;
    ctx.viewport_h = 300.0f;

    float x = 0.0f;
    float y = 0.0f;
    spectra_overlay_place_label(&ctx, 300.0f, 200.0f, 80.0f, 20.0f, 8.0f, 2.0f, &x, &y);
    EXPECT_FLOAT_EQ(x, 308.0f);
    EXPECT_FLOAT_EQ(y, 172.0f);
}

TEST(PluginOverlayLabel, StaysInsideAtAllCorners)
{
    SpectraOverlayContext ctx{};
    ctx.viewport_x = 100.0f;
    ctx.viewport_y = 50.0f;
    ctx.viewport_w = 400.0f;
    ctx.viewport_h = 300.0f;

    const float w   = 80.0f;
    const float h   = 20.0f;
    const float pad = 2.0f;

    const float vx0 = ctx.viewport_x;
    const float vy0 = ctx.viewport_y;
    const float vx1 = ctx.viewport_x + ctx.viewport_w;
    const float vy1 = ctx.viewport_y + ctx.viewport_h;

    const float anchors[][2] = {
        {vx0, vy0},
        {vx1, vy0},
        {vx0, vy1},
        {vx1, vy1},
        {vx1 - 1.0f, vy0 + 1.0f},
        {(vx0 + vx1) * 0.5f, (vy0 + vy1) * 0.5f},
    };
    for (const auto& a : anchors)
    {
        float x = 0.0f;
        float y = 0.0f;
        spectra_overlay_place_label(&ctx, a[0], a[1], w, h, 8.0f, pad, &x, &y);
        SCOPED_TRACE(::testing::Message() << "anchor (" << a[0] << ", " << a[1] << ")");
        EXPECT_GE(x, vx0 + pad);
        EXPECT_GE(y, vy0 + pad);
        EXPECT_LE(x + w, vx1 - pad);
        EXPECT_LE(y + h, vy1 - pad);
    }

    // Top-right corner flips the label to the left of and below the cursor.
    float x = 0.0f;
    float y = 0.0f;
    spectra_overlay_place_label(&ctx, vx1 - 1.0f, vy0 + 1.0f, w, h, 8.0f, pad, &x, &y);
    EXPECT_LT(x + w, vx1 - 1.0f);
    EXPECT_GT(y, vy0 + 1.0f);
}

TEST(PluginOverlayLabel, OversizedLabelPinsToTopLeft)
{
    SpectraOverlayContext ctx{};
    ctx.viewport_x = 10.0f;
    ctx.viewport_y = 20.0f;
    ctx.viewport_w = 50.0f;
    ctx.viewport_h = 30.0f;

    float x = 0.0f;
    float y = 0.0f;
    spectra_overlay_place_label(&ctx, 30.0f, 30.0f, 200.0f, 100.0f, 8.0f, 0.0f, &x, &y);
    EXPECT_FLOAT_EQ(x, 10.0f);
    EXPECT_FLOAT_EQ(y, 20.0f);
}
