// test_example_overlay_crosshair.cpp — Draws the shipped crosshair overlay example
// (examples/plugins/overlay_crosshair.cpp) into a real ImGui draw list and checks that
// it follows the host theme and keeps its coordinate label inside the axes.
//
// The plug-in is loaded with dlopen and initialised with a hand-built context that
// reports a v2.2 host: that satisfies both the example's registration guard and its
// v2.1 appearance check without depending on the PluginManager's version reporting.

#include <gtest/gtest.h>
#include <imgui.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ui/overlay/overlay_registry.hpp"
#include "ui/theme/theme.hpp"
#include "ui/workspace/plugin_api.hpp"

#ifndef _WIN32
    #include <dlfcn.h>
#endif

using namespace spectra;

#if defined(SPECTRA_EXAMPLE_OVERLAY_CROSSHAIR_PATH) && !defined(_WIN32)

namespace
{

constexpr float kVx = 100.0f;
constexpr float kVy = 50.0f;
constexpr float kVw = 400.0f;
constexpr float kVh = 300.0f;

struct Bounds
{
    int   count = 0;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
};

class ExampleCrosshairTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ImGui::CreateContext();
        ImGuiIO& io    = ImGui::GetIO();
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.DeltaTime   = 1.0f / 60.0f;
        // No renderer uploads textures here; let the atlas build glyphs on demand.
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefault();

        ui::ThemeManager::set_current(&tm_);
        tm_.ensure_initialized();

        handle_ = dlopen(SPECTRA_EXAMPLE_OVERLAY_CROSSHAIR_PATH, RTLD_NOW | RTLD_LOCAL);
        ASSERT_NE(handle_, nullptr) << dlerror();
        auto init = reinterpret_cast<SpectraPluginInitFn>(dlsym(handle_, "spectra_plugin_init"));
        ASSERT_NE(init, nullptr);

        SpectraPluginContext ctx{};
        ctx.api_version_major = 2;
        ctx.api_version_minor = 2;
        ctx.overlay_registry  = static_cast<SpectraOverlayRegistry>(&reg_);
        SpectraPluginInfo info{};
        ASSERT_EQ(init(&ctx, &info), 0);
        ASSERT_EQ(reg_.count(), 1u) << "example did not register its overlay";
    }

    void TearDown() override
    {
        // Drop the plug-in's callbacks before its code is unmapped.
        for (const auto& name : reg_.overlay_names())
            reg_.unregister_overlay(name);
        if (handle_)
            dlclose(handle_);
        ui::ThemeManager::set_current(nullptr);
        ImGui::DestroyContext();
    }

    // Draw one frame of the overlay with the cursor at (mx, my); returns the vertices.
    std::vector<ImDrawVert> draw(float mx, float my, float scale)
    {
        ImGui::GetStyle().FontScaleMain = scale;
        ImGui::NewFrame();
        ImDrawList*  dl    = ImGui::GetBackgroundDrawList();
        const size_t first = static_cast<size_t>(dl->VtxBuffer.Size);

        OverlayDrawContext ctx{};
        ctx.viewport_x = kVx;
        ctx.viewport_y = kVy;
        ctx.viewport_w = kVw;
        ctx.viewport_h = kVh;
        ctx.mouse_x    = mx;
        ctx.mouse_y    = my;
        ctx.is_hovered = true;
        ctx.draw_list  = dl;
        ctx.theme      = &tm_.colors();
        ctx.ui_scale   = scale;
        ctx.font_size  = ImGui::GetFontSize();
        reg_.draw_all(ctx);

        std::vector<ImDrawVert> out(dl->VtxBuffer.begin() + first, dl->VtxBuffer.end());
        ImGui::EndFrame();
        return out;
    }

    static Bounds bounds_of(const std::vector<ImDrawVert>& v, ImU32 col)
    {
        Bounds b;
        for (const auto& vert : v)
        {
            if (vert.col != col)
                continue;
            ++b.count;
            b.x0 = std::min(b.x0, vert.pos.x);
            b.y0 = std::min(b.y0, vert.pos.y);
            b.x1 = std::max(b.x1, vert.pos.x);
            b.y1 = std::max(b.y1, vert.pos.y);
        }
        return b;
    }

    static uint32_t pack(const ui::Color& c)
    {
        auto ch = [](float v)
        { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return spectra_overlay_pack_rgba(ch(c.r), ch(c.g), ch(c.b), ch(c.a));
    }

    ui::ThemeManager tm_;
    OverlayRegistry  reg_;
    void*            handle_ = nullptr;
};

}   // namespace

TEST(ExampleCrosshairColor, PackHelperMatchesImGuiByteOrder)
{
    EXPECT_EQ(SPECTRA_OVERLAY_COLOR(0x12, 0x34, 0x56, 0x78), IM_COL32(0x12, 0x34, 0x56, 0x78));
    EXPECT_EQ(spectra_overlay_pack_rgba(255, 255, 0, 255), IM_COL32(255, 255, 0, 255));

    const ImVec4 yellow = ImGui::ColorConvertU32ToFloat4(SPECTRA_OVERLAY_COLOR(255, 255, 0, 255));
    EXPECT_FLOAT_EQ(yellow.x, 1.0f);
    EXPECT_FLOAT_EQ(yellow.y, 1.0f);
    EXPECT_FLOAT_EQ(yellow.z, 0.0f);
    EXPECT_FLOAT_EQ(yellow.w, 1.0f);
}

TEST_F(ExampleCrosshairTest, HostFillsAppearanceFields)
{
    // Goes through the same host spectra_register_overlay path the example uses.
    struct Probe
    {
        SpectraOverlayContext ctx{};
        int                   calls = 0;
    } probe;
    auto cb = [](const SpectraOverlayContext* c, void* ud)
    {
        auto* p = static_cast<Probe*>(ud);
        p->ctx  = *c;
        ++p->calls;
    };
    ASSERT_EQ(
        spectra_register_overlay(static_cast<SpectraOverlayRegistry>(&reg_), "Probe", cb, &probe),
        0);

    tm_.set_theme("light");
    draw(kVx + 10.0f, kVy + 10.0f, 2.0f);
    ASSERT_EQ(probe.calls, 1);
    EXPECT_TRUE(probe.ctx.flags & SPECTRA_OVERLAY_FLAG_HAS_THEME);
    EXPECT_EQ(probe.ctx.theme.crosshair, pack(tm_.colors().crosshair));
    EXPECT_FLOAT_EQ(probe.ctx.ui_scale, 2.0f);
    EXPECT_GT(probe.ctx.font_size, 0.0f);
    ASSERT_NE(probe.ctx.measure_text, nullptr);
}

TEST_F(ExampleCrosshairTest, UsesThemeColoursOnEveryTheme)
{
    for (const char* theme_name : {"dark", "light"})
    {
        SCOPED_TRACE(theme_name);
        tm_.set_theme(theme_name);
        const auto& colors = tm_.colors();

        const auto verts = draw(kVx + kVw * 0.5f, kVy + kVh * 0.5f, 1.0f);
        ASSERT_FALSE(verts.empty());

        EXPECT_GT(bounds_of(verts, pack(colors.crosshair)).count, 0) << "crosshair not themed";
        EXPECT_GT(bounds_of(verts, pack(colors.text_primary)).count, 0) << "label text not themed";
        // The old hard-coded white line / "yellow" (actually cyan) label must be gone.
        if (pack(colors.crosshair) != 0xCCFFFFFFu)
        {
            EXPECT_EQ(bounds_of(verts, 0xCCFFFFFFu).count, 0);
        }
        EXPECT_EQ(bounds_of(verts, 0xFFFFFF00u).count, 0);
    }
}

TEST_F(ExampleCrosshairTest, LabelStaysInsideAxesAtAllCorners)
{
    tm_.set_theme("light");
    const auto&    colors   = tm_.colors();
    const uint32_t text_col = pack(colors.text_primary);
    const uint32_t bg_col   = spectra_overlay_color_scale_alpha(pack(colors.tooltip_bg), 0.92f);

    const float pts[][2] = {
        {kVx + 1.0f, kVy + 1.0f},
        {kVx + kVw - 1.0f, kVy + 1.0f},
        {kVx + 1.0f, kVy + kVh - 1.0f},
        {kVx + kVw - 1.0f, kVy + kVh - 1.0f},
        {kVx + kVw * 0.5f, kVy + kVh * 0.5f},
    };
    for (float scale : {1.0f, 2.0f})
    {
        for (const auto& p : pts)
        {
            SCOPED_TRACE(::testing::Message()
                         << "scale " << scale << " cursor (" << p[0] << ", " << p[1] << ")");
            const auto verts = draw(p[0], p[1], scale);

            for (uint32_t col : {text_col, bg_col})
            {
                const Bounds b = bounds_of(verts, col);
                ASSERT_GT(b.count, 0);
                EXPECT_GE(b.x0, kVx);
                EXPECT_GE(b.y0, kVy);
                EXPECT_LE(b.x1, kVx + kVw);
                EXPECT_LE(b.y1, kVy + kVh);
            }
        }
    }
}

TEST_F(ExampleCrosshairTest, LabelGrowsWithUiScale)
{
    tm_.set_theme("dark");
    const uint32_t bg_col = spectra_overlay_color_scale_alpha(pack(tm_.colors().tooltip_bg), 0.92f);

    const Bounds b1 = bounds_of(draw(kVx + 100.0f, kVy + 150.0f, 1.0f), bg_col);
    const Bounds b2 = bounds_of(draw(kVx + 100.0f, kVy + 150.0f, 2.0f), bg_col);
    ASSERT_GT(b1.count, 0);
    ASSERT_GT(b2.count, 0);
    EXPECT_NEAR(b2.y1 - b2.y0, 2.0f * (b1.y1 - b1.y0), 2.0f);
}

#else

TEST(ExampleCrosshair, SkippedWithoutExamplePlugin)
{
    GTEST_SKIP() << "overlay_crosshair example plug-in not built";
}

#endif
