// Many windows' ImGui contexts alive at once must not run out of descriptor
// sets.  Each context needs a combined-image-sampler set for its font atlas and
// one for the logo; the renderer's shared pool holds only 32 such descriptors,
// so with ImGui allocating from it the ~17th window failed
// vkAllocateDescriptorSets and crashed in vkUpdateDescriptorSets (QA fuzz,
// 2026-11-21).  Each ImGui context now owns a private pool.

#include <gtest/gtest.h>

#ifdef SPECTRA_USE_IMGUI

    #include <memory>
    #include <vector>

    #include "imgui.h"
    #include "render/vulkan/vk_backend.hpp"
    #include "ui/imgui/imgui_integration.hpp"
    #include "ui/theme/theme.hpp"

using namespace spectra;

TEST(ImGuiDescriptorPoolGpu, ManyContextsEachRegisterTheirLogo)
{
    VulkanBackend backend;
    if (!backend.init(true))
        GTEST_SKIP() << "No Vulkan device";
    ASSERT_TRUE(backend.create_offscreen_framebuffer(320, 240));

    ui::ThemeManager tm;
    ui::ThemeManager::set_current(&tm);
    tm.ensure_initialized();

    {
        constexpr int                                  kContexts = 24;   // 48 sampler sets
        std::vector<std::unique_ptr<ImGuiIntegration>> uis;
        for (int i = 0; i < kContexts; ++i)
        {
            auto ui = std::make_unique<ImGuiIntegration>();
            ui->set_theme_manager(&tm);
            ASSERT_TRUE(ui->init_headless(backend, 320, 240)) << "context " << i;
            EXPECT_TRUE(ui->logo_texture_registered()) << "context " << i;
            uis.push_back(std::move(ui));
        }
        // Tear down in creation order, then make sure a fresh context still works.
        for (auto& ui : uis)
            ui->shutdown();
        uis.clear();

        ImGuiIntegration again;
        again.set_theme_manager(&tm);
        ASSERT_TRUE(again.init_headless(backend, 320, 240));
        EXPECT_TRUE(again.logo_texture_registered());
        again.shutdown();
    }

    ImGui::SetCurrentContext(nullptr);
    ui::ThemeManager::set_current(nullptr);
    backend.shutdown();
}

#endif   // SPECTRA_USE_IMGUI
