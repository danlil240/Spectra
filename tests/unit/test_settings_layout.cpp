// Settings window sizing (audit Q04): the modal never exceeds the viewport
// work area, its tab body scrolls and the footer actions stay reachable.

#include <gtest/gtest.h>
#include <string_view>

#include "ui/settings/settings_layout.hpp"

#if defined(SPECTRA_USE_IMGUI) && __has_include(<imgui.h>)
    #define SPECTRA_TEST_IMGUI_SETTINGS 1
    #include <imgui.h>
    #include <imgui_internal.h>

    #include "ui/commands/shortcut_config.hpp"
    #include "ui/commands/shortcut_manager.hpp"
    #include "ui/settings/settings_panel.hpp"
    #include "ui/settings/settings_store.hpp"
    #include "ui/theme/theme.hpp"
#endif

using namespace spectra::ui::settings;

// ─── Pure sizing math ────────────────────────────────────────────────────────

TEST(SettingsLayout, LargeScreenKeepsNaturalContentSize)
{
    const SettingsVec2 s = compute_settings_window_size({2560.0f, 1440.0f}, {500.0f, 610.0f}, 1.0f);
    EXPECT_FLOAT_EQ(s.x, 500.0f);
    EXPECT_FLOAT_EQ(s.y, 610.0f);
}

TEST(SettingsLayout, LargeScreenKeepsDesignCap)
{
    const SettingsVec2 s =
        compute_settings_window_size({2560.0f, 1440.0f}, {500.0f, 1300.0f}, 1.0f);
    EXPECT_FLOAT_EQ(s.y, k_settings_max_height);
}

TEST(SettingsLayout, NarrowContentGetsMinimumWidth)
{
    const SettingsVec2 s = compute_settings_window_size({1920.0f, 1080.0f}, {200.0f, 300.0f}, 1.0f);
    EXPECT_FLOAT_EQ(s.x, k_settings_min_width);
}

TEST(SettingsLayout, CapsHeightTo720WorkAreaWithMargin)
{
    const SettingsWindowLimits lim = compute_settings_window_limits({1280.0f, 720.0f}, 1.0f);
    EXPECT_FLOAT_EQ(lim.margin, k_settings_margin);
    EXPECT_FLOAT_EQ(lim.max_size.y, 720.0f - 2.0f * k_settings_margin);

    const SettingsVec2 s = compute_settings_window_size({1280.0f, 720.0f}, {440.0f, 900.0f}, 1.0f);
    EXPECT_LE(s.y, 720.0f - 2.0f * k_settings_margin);
}

TEST(SettingsLayout, SmallWindowsFitInsideWorkArea)
{
    for (const SettingsVec2 work : {SettingsVec2{640.0f, 480.0f}, SettingsVec2{320.0f, 240.0f}})
    {
        const SettingsWindowLimits lim = compute_settings_window_limits(work, 1.0f);
        const SettingsVec2         s   = compute_settings_window_size(work, {800.0f, 900.0f}, 1.0f);
        EXPECT_GT(lim.margin, 0.0f);
        EXPECT_LE(s.x + 2.0f * lim.margin, work.x + 1e-3f);
        EXPECT_LE(s.y + 2.0f * lim.margin, work.y + 1e-3f);
        // The minimum width never forces the window past the work area.
        EXPECT_LE(lim.min_size.x, lim.max_size.x);
    }
    // 320x240: the margin shrinks (5% per side) instead of eating 48px.
    const SettingsWindowLimits tiny = compute_settings_window_limits({320.0f, 240.0f}, 1.0f);
    EXPECT_FLOAT_EQ(tiny.margin, 12.0f);
    EXPECT_FLOAT_EQ(tiny.max_size.x, 296.0f);
    EXPECT_FLOAT_EQ(tiny.max_size.y, 216.0f);
}

TEST(SettingsLayout, DisplayScalingScalesDesignValuesButNotWorkArea)
{
    // 2x scale on a large screen: design min width / cap double.
    const SettingsWindowLimits big = compute_settings_window_limits({3840.0f, 2160.0f}, 2.0f);
    EXPECT_FLOAT_EQ(big.min_size.x, 2.0f * k_settings_min_width);
    EXPECT_FLOAT_EQ(big.max_size.y, 2.0f * k_settings_max_height);
    EXPECT_FLOAT_EQ(big.margin, 2.0f * k_settings_margin);

    // 2x scale on 1280x720: still bounded by the work area.
    const SettingsVec2 s =
        compute_settings_window_size({1280.0f, 720.0f}, {2000.0f, 2000.0f}, 2.0f);
    EXPECT_LE(s.x, 1280.0f);
    EXPECT_LE(s.y, 720.0f);
    EXPECT_FLOAT_EQ(s.y, 720.0f - 2.0f * 36.0f);   // margin = min(48, 5% of 720)
}

TEST(SettingsLayout, InvalidScaleAndDegenerateWorkAreaAreSafe)
{
    const SettingsWindowLimits a = compute_settings_window_limits({1280.0f, 720.0f}, 0.0f);
    EXPECT_FLOAT_EQ(a.min_size.x, k_settings_min_width);
    const SettingsWindowLimits b = compute_settings_window_limits({1280.0f, 720.0f}, -3.0f);
    EXPECT_FLOAT_EQ(b.min_size.x, k_settings_min_width);

    const SettingsWindowLimits z = compute_settings_window_limits({0.0f, 0.0f}, 1.0f);
    EXPECT_GE(z.max_size.x, 1.0f);
    EXPECT_GE(z.max_size.y, 1.0f);
    EXPECT_LE(z.min_size.x, z.max_size.x);
}

TEST(SettingsLayout, BodyHeightLeavesRoomForChrome)
{
    EXPECT_FLOAT_EQ(compute_settings_body_max_height(672.0f, 120.0f, 40.0f), 552.0f);
    // Not enough room: keep a minimum body (the window then scrolls).
    EXPECT_FLOAT_EQ(compute_settings_body_max_height(100.0f, 120.0f, 40.0f), 40.0f);
    // The minimum body never exceeds the window itself.
    EXPECT_FLOAT_EQ(compute_settings_body_max_height(30.0f, 120.0f, 40.0f), 30.0f);
}

TEST(SettingsLayout, ClampPosKeepsWindowInsideWorkArea)
{
    const SettingsVec2 wp{0.0f, 0.0f}, ws{1280.0f, 720.0f};
    // Window grew downward past the bottom edge: pulled back up.
    SettingsVec2 p = clamp_settings_window_pos({430.0f, 300.0f}, {420.0f, 600.0f}, wp, ws, 24.0f);
    EXPECT_FLOAT_EQ(p.x, 430.0f);
    EXPECT_FLOAT_EQ(p.y, 720.0f - 24.0f - 600.0f);
    // Off the top-left: pushed in.
    p = clamp_settings_window_pos({-50.0f, -10.0f}, {420.0f, 300.0f}, wp, ws, 24.0f);
    EXPECT_FLOAT_EQ(p.x, 24.0f);
    EXPECT_FLOAT_EQ(p.y, 24.0f);
    // Larger than the work area: pinned to the work origin.
    p = clamp_settings_window_pos({100.0f, 100.0f}, {2000.0f, 900.0f}, wp, ws, 24.0f);
    EXPECT_FLOAT_EQ(p.x, 0.0f);
    EXPECT_FLOAT_EQ(p.y, 0.0f);
    // Offset work area (e.g. a menu bar reserving the top).
    p = clamp_settings_window_pos({0.0f, 0.0f}, {420.0f, 300.0f}, {0.0f, 40.0f}, ws, 24.0f);
    EXPECT_FLOAT_EQ(p.y, 64.0f);
}

// ─── Live ImGui panel ────────────────────────────────────────────────────────

#ifdef SPECTRA_TEST_IMGUI_SETTINGS

namespace
{

class SettingsPanelImGui : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ctx_           = ImGui::CreateContext();
        ImGuiIO& io    = ImGui::GetIO();
        io.DeltaTime   = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.Fonts->AddFontDefault();
        unsigned char* px = nullptr;
        int            tw = 0, th = 0;
        io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);

        tm_.ensure_initialized();
        spectra::ui::ThemeManager::set_current(&tm_);
        shortcuts_.register_defaults();
        shortcut_cfg_.set_shortcut_manager(&shortcuts_);

        panel_.set_settings_store(&store_);
        panel_.set_theme_manager(&tm_);
        panel_.set_shortcut_config(&shortcut_cfg_);
    }

    void TearDown() override
    {
        spectra::ui::ThemeManager::set_current(nullptr);
        ImGui::DestroyContext(ctx_);
    }

    void pump(float w, float h, int frames = 8)
    {
        ImGui::GetIO().DisplaySize = ImVec2(w, h);
        for (int i = 0; i < frames; ++i)
        {
            ImGui::NewFrame();
            panel_.draw();
            ImGui::Render();
        }
    }

    static ImGuiWindow* settings_window() { return ImGui::FindWindowByName("Settings##spectra"); }

    static ImGuiWindow* body_window(ImGuiWindow* parent)
    {
        for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
        {
            if (w->ParentWindow == parent && (w->Flags & ImGuiWindowFlags_ChildWindow)
                && std::string_view(w->Name).find("##settings_body") != std::string_view::npos
                && w->Active)
                return w;
        }
        return nullptr;
    }

    // Window fully on screen; the footer (drawn in the parent after the body)
    // is reachable without scrolling the window itself.
    void expect_fits(float w, float h)
    {
        ImGuiWindow* win = settings_window();
        ASSERT_NE(win, nullptr);
        EXPECT_GE(win->Pos.x, -0.5f);
        EXPECT_GE(win->Pos.y, -0.5f);
        EXPECT_LE(win->Pos.x + win->Size.x, w + 0.5f) << "width " << win->Size.x;
        EXPECT_LE(win->Pos.y + win->Size.y, h + 0.5f) << "height " << win->Size.y;
        EXPECT_LE(win->ScrollMax.y, 0.5f) << "footer pushed below the window";
    }

    ImGuiContext*             ctx_ = nullptr;
    spectra::ui::ThemeManager tm_;
    SettingsStore             store_;
    spectra::ShortcutManager  shortcuts_;
    spectra::ShortcutConfig   shortcut_cfg_;
    SettingsPanel             panel_;
};

}   // namespace

TEST_F(SettingsPanelImGui, LargeScreenAutoSizesWithoutScrolling)
{
    panel_.open();
    pump(2560.0f, 1600.0f);
    expect_fits(2560.0f, 1600.0f);

    ImGuiWindow* win = settings_window();
    ASSERT_NE(win, nullptr);
    EXPECT_LT(win->Size.y, k_settings_max_height);
    ImGuiWindow* body = body_window(win);
    ASSERT_NE(body, nullptr);
    EXPECT_LE(body->ScrollMax.y, 0.5f) << "appearance body should fit on a large screen";
}

TEST_F(SettingsPanelImGui, AppearanceFitsSmallViewportsAndBodyScrolls)
{
    for (const ImVec2 sz : {ImVec2(640.0f, 480.0f), ImVec2(320.0f, 240.0f)})
    {
        SCOPED_TRACE(testing::Message() << sz.x << "x" << sz.y);
        panel_.select_tab(0);
        panel_.open();
        pump(sz.x, sz.y);
        expect_fits(sz.x, sz.y);

        ImGuiWindow* body = body_window(settings_window());
        ASSERT_NE(body, nullptr);
        EXPECT_GT(body->ScrollMax.y, 0.0f) << "appearance controls must scroll, not be cut off";
    }
}

TEST_F(SettingsPanelImGui, DisplayScaling2xAt720Fits)
{
    ImGui::GetStyle().FontScaleDpi = 2.0f;
    panel_.open();
    pump(1280.0f, 720.0f);
    expect_fits(1280.0f, 720.0f);
    ImGuiWindow* body = body_window(settings_window());
    ASSERT_NE(body, nullptr);
    EXPECT_GT(body->ScrollMax.y, 0.0f);
}

TEST_F(SettingsPanelImGui, EveryTabFits720AndSmallWindows)
{
    for (const ImVec2 sz :
         {ImVec2(1280.0f, 720.0f), ImVec2(640.0f, 480.0f), ImVec2(320.0f, 240.0f)})
    {
        for (int tab = 0; tab < 3; ++tab)
        {
            SCOPED_TRACE(testing::Message() << sz.x << "x" << sz.y << " tab " << tab);
            panel_.select_tab(tab);
            panel_.open();
            pump(sz.x, sz.y);
            expect_fits(sz.x, sz.y);
        }
    }
}

// The app renders on demand, so a capture (or the user) may see the second
// frame after opening; the window must already be at its final size then.
TEST_F(SettingsPanelImGui, SettlesOnSecondFrameAfterOpening)
{
    for (int tab = 0; tab < 3; ++tab)
    {
        SCOPED_TRACE(testing::Message() << "tab " << tab);
        panel_.set_visible(false);
        pump(1280.0f, 720.0f, 2);
        panel_.select_tab(tab);
        panel_.open();
        pump(1280.0f, 720.0f, 2);
        expect_fits(1280.0f, 720.0f);
        ImGuiWindow* win = settings_window();
        ASSERT_NE(win, nullptr);
        // No stale scrollbar on a body whose content fits.
        ImGuiWindow* body = body_window(win);
        ASSERT_NE(body, nullptr);
        EXPECT_EQ(body->ScrollbarY, body->ContentSize.y > body->Size.y + 0.5f);
        const ImVec2 early_pos = win->Pos, early_size = win->Size;
        pump(1280.0f, 720.0f, 6);
        EXPECT_FLOAT_EQ(win->Size.x, early_size.x);
        EXPECT_FLOAT_EQ(win->Size.y, early_size.y);
        EXPECT_FLOAT_EQ(win->Pos.y, early_pos.y);
    }
}

TEST_F(SettingsPanelImGui, ShrinkingViewportWhileOpenKeepsWindowOnScreen)
{
    panel_.open();
    pump(1920.0f, 1080.0f);
    pump(640.0f, 480.0f);
    expect_fits(640.0f, 480.0f);
    // The width stays at its natural value instead of latching to the maximum
    // when the height becomes constrained.
    ImGuiWindow* win = settings_window();
    ASSERT_NE(win, nullptr);
    EXPECT_FLOAT_EQ(win->Size.x, k_settings_min_width);
}

#endif   // SPECTRA_TEST_IMGUI_SETTINGS
