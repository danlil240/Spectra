// Welcome screen (no figure open): the command palette opened with Ctrl+K /
// Tools > Command Palette is drawn, and the no-shell menus come from the shared
// menu model (issue #107). Needs a Vulkan device for ImGuiIntegration.

#include <gtest/gtest.h>

#ifdef SPECTRA_USE_IMGUI

    #include "imgui.h"
    #include "imgui_internal.h"
    #include "render/vulkan/vk_backend.hpp"
    #include "ui/commands/command_palette.hpp"
    #include "ui/commands/command_registry.hpp"
    #include "ui/imgui/imgui_integration.hpp"
    #include "ui/shell/menu_bar.hpp"
    #include "ui/theme/theme.hpp"

using namespace spectra;

TEST(UiShellWelcomeGpu, CommandPaletteDrawsWithoutAFigure)
{
    VulkanBackend backend;
    if (!backend.init(true))
        GTEST_SKIP() << "No Vulkan device";
    ASSERT_TRUE(backend.create_offscreen_framebuffer(800, 600));

    ui::ThemeManager tm;
    ui::ThemeManager::set_current(&tm);
    tm.ensure_initialized();

    {
        CommandRegistry reg;
        bool            ran = false;
        reg.register_command("test.cmd", "Test Command", [&]() { ran = true; });
        CommandPalette palette;
        palette.set_command_registry(&reg);

        ImGuiIntegration ui;
        ui.set_theme_manager(&tm);
        ui.set_command_registry(&reg);
        ui.set_command_palette(&palette);
        ASSERT_TRUE(ui.init_headless(backend, 800, 600));

        palette.open();   // what Ctrl+K (app.command_palette) does
        for (int frame = 0; frame < 2; ++frame)
        {
            ImGuiIntegration::HeadlessFrameInput fi{};
            fi.display_w = 800.0f;
            fi.display_h = 600.0f;
            ui.new_frame_headless(fi);
            ui.build_empty_ui();

            ImGuiWindow* w = ImGui::FindWindowByName("##command_palette");
            ASSERT_NE(w, nullptr) << "palette window was not drawn on the welcome screen";
            EXPECT_TRUE(w->Active);
            ImGui::EndFrame();
        }
        EXPECT_FALSE(ran);

        // No app shell is bound here: the menus came from the shared model.
        const auto names = ui.fallback_menu_bar().menu_names();
        ASSERT_FALSE(names.empty());
        EXPECT_EQ(names.front(), "File");
    }

    ui::ThemeManager::set_current(nullptr);
    backend.shutdown();
}

#endif   // SPECTRA_USE_IMGUI
