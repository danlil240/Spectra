// Canvas overlays (split-pane outlines, splitters, rubber bands, ...) draw
// above the canvas-level windows but below floating panels, menus, modals and
// tooltips.

#include <gtest/gtest.h>

#if defined(SPECTRA_USE_IMGUI) && __has_include(<imgui.h>)

    #include <imgui.h>
    #include <imgui_internal.h>

    #include "ui/overlay/canvas_overlay_layer.hpp"

namespace
{

constexpr ImGuiWindowFlags k_canvas_flags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
    | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;

class CanvasOverlayLayerTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ImGui::CreateContext();
        ImGuiIO& io    = ImGui::GetIO();
        io.DisplaySize = ImVec2(1000.0f, 700.0f);
        io.DeltaTime   = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.Fonts->AddFontDefault();
        unsigned char* px = nullptr;
        int            tw = 0, th = 0;
        io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);
    }

    void TearDown() override { ImGui::DestroyContext(); }

    // One frame shaped like the app's: a canvas-level pane header, the
    // canvas overlays, then a floating panel and (optionally) a modal.
    ImDrawList* run_frame(bool with_modal)
    {
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(100.0f, 100.0f));
        ImGui::SetNextWindowSize(ImVec2(400.0f, 30.0f));
        ImGui::Begin("##pane_header", nullptr, k_canvas_flags);
        ImGui::TextUnformatted("Figure 1");
        ImGui::End();

        ImDrawList* layer_dl = spectra::ui::canvas_overlay_draw_list();
        layer_dl->AddRect(ImVec2(500.0f, 100.0f), ImVec2(900.0f, 600.0f), IM_COL32_WHITE);

        ImGui::SetNextWindowPos(ImVec2(300.0f, 200.0f));
        ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
        ImGui::Begin("Floating panel");
        ImGui::TextUnformatted("panel");
        ImGui::End();

        if (with_modal)
        {
            // Modals need a window to open from.
            ImGui::Begin("##popup_host", nullptr, k_canvas_flags | ImGuiWindowFlags_NoBackground);
            ImGui::OpenPopup("Settings##modal");
            ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f));
            if (ImGui::BeginPopupModal("Settings##modal"))
            {
                ImGui::TextUnformatted("settings");
                ImGui::EndPopup();
            }
            ImGui::End();
        }

        ImGui::Render();
        return layer_dl;
    }

    static int draw_list_index(ImDrawList* dl)
    {
        ImDrawData* data = ImGui::GetDrawData();
        for (int i = 0; i < data->CmdListsCount; ++i)
            if (data->CmdLists[i] == dl)
                return i;
        return -1;
    }

    static int draw_list_index(const char* window_name)
    {
        ImGuiWindow* w = ImGui::FindWindowByName(window_name);
        return w ? draw_list_index(w->DrawList) : -1;
    }
};

}   // namespace

TEST_F(CanvasOverlayLayerTest, DrawsAbovePaneHeadersAndBelowFloatingPanels)
{
    for (int frame = 0; frame < 3; ++frame)
    {
        ImDrawList* layer = run_frame(false);
        const int   idx   = draw_list_index(layer);
        ASSERT_GE(idx, 0) << "frame " << frame;
        EXPECT_GT(idx, draw_list_index("##pane_header")) << "frame " << frame;
        EXPECT_LT(idx, draw_list_index("Floating panel")) << "frame " << frame;
    }
}

TEST_F(CanvasOverlayLayerTest, DrawsBelowModals)
{
    for (int frame = 0; frame < 3; ++frame)
    {
        ImDrawList* layer = run_frame(true);
        const int   idx   = draw_list_index(layer);
        ASSERT_GE(idx, 0) << "frame " << frame;
        // A popup's first frame is hidden while ImGui measures it.
        if (frame == 0)
            continue;
        const int modal = draw_list_index("Settings##modal");
        ASSERT_GE(modal, 0) << "frame " << frame;
        EXPECT_LT(idx, modal) << "frame " << frame;
    }
}

TEST_F(CanvasOverlayLayerTest, NeverUsesTheForegroundDrawList)
{
    run_frame(false);
    ImGui::NewFrame();
    ImDrawList* layer = spectra::ui::canvas_overlay_draw_list();
    EXPECT_NE(layer, ImGui::GetForegroundDrawList());
    EXPECT_EQ(layer, spectra::ui::canvas_overlay_draw_list());
    ImGui::Render();
}

#else

TEST(CanvasOverlayLayerTest, SkippedWithoutImGui)
{
    GTEST_SKIP() << "ImGui not enabled";
}

#endif
