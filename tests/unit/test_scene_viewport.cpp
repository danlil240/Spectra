#include <cmath>

#include <gtest/gtest.h>

#include <spectra/camera.hpp>
#include <spectra/series.hpp>

#include "adapters/ros2/scene/scene_manager.hpp"
#include "adapters/ros2/ui/scene_viewport.hpp"

#if defined(SPECTRA_USE_IMGUI) && __has_include(<imgui.h>)
    #define SPECTRA_SCENE_VIEWPORT_IMGUI_TESTS 1
    #include <imgui.h>

    #include "ui/theme/theme.hpp"
#endif

namespace ros2 = spectra::adapters::ros2;

TEST(SceneViewportTest, DefaultCameraUsesZUpAxis)
{
    ros2::SceneViewport viewport;

    EXPECT_EQ(viewport.camera().up_axis, spectra::Camera::UpAxis::Z);
    EXPECT_FLOAT_EQ(viewport.camera().up.x, 0.0f);
    EXPECT_FLOAT_EQ(viewport.camera().up.y, 0.0f);
    EXPECT_FLOAT_EQ(viewport.camera().up.z, 1.0f);
}

TEST(SceneViewportTest, SetCameraNormalizesRestoredCameraToZUp)
{
    ros2::SceneViewport viewport;

    // Camera defaults to Z-up; simulate a camera restored from a Y-up session.
    spectra::Camera restored;
    restored.set_up_axis(spectra::Camera::UpAxis::Y);
    restored.reset();
    restored.set_azimuth(90.0f);
    restored.set_elevation(30.0f);
    restored.set_distance(12.0f);
    restored.set_target({1.0f, 2.0f, 3.0f});
    restored.set_projection(spectra::Camera::ProjectionMode::Orthographic);
    restored.set_fov(55.0f);

    ASSERT_EQ(restored.up_axis, spectra::Camera::UpAxis::Y);

    viewport.set_camera(restored);

    const spectra::Camera& camera = viewport.camera();
    EXPECT_EQ(camera.up_axis, spectra::Camera::UpAxis::Z);
    EXPECT_FLOAT_EQ(camera.azimuth, 90.0f);
    EXPECT_FLOAT_EQ(camera.elevation, 30.0f);
    EXPECT_FLOAT_EQ(camera.distance, 12.0f);
    EXPECT_FLOAT_EQ(camera.target.x, 1.0f);
    EXPECT_FLOAT_EQ(camera.target.y, 2.0f);
    EXPECT_FLOAT_EQ(camera.target.z, 3.0f);
    EXPECT_EQ(camera.projection_mode, spectra::Camera::ProjectionMode::Orthographic);
    EXPECT_FLOAT_EQ(camera.fov, 55.0f);
    EXPECT_FLOAT_EQ(camera.up.x, 0.0f);
    EXPECT_FLOAT_EQ(camera.up.y, 0.0f);
    EXPECT_FLOAT_EQ(camera.up.z, 1.0f);

    EXPECT_NEAR(camera.position.x, 1.0f, 1e-4f);
    EXPECT_NEAR(camera.position.y, 2.0f + 12.0f * std::cos(spectra::deg_to_rad(30.0f)), 1e-4f);
    EXPECT_NEAR(camera.position.z, 3.0f + 12.0f * std::sin(spectra::deg_to_rad(30.0f)), 1e-4f);
}

TEST(SceneViewportTest, SoftwarePreviewPaintsCanvasWithoutGpuRenderer)
{
    constexpr ros2::SceneCanvasPaint paint = ros2::scene_canvas_paint(false);
    EXPECT_TRUE(paint.opaque_window_bg);
    EXPECT_TRUE(paint.fill_canvas_background);
    EXPECT_TRUE(paint.draw_preview_geometry);
}

TEST(SceneViewportTest, GpuRendererLeavesCanvasTransparent)
{
    constexpr ros2::SceneCanvasPaint paint = ros2::scene_canvas_paint(true);
    EXPECT_FALSE(paint.opaque_window_bg);
    EXPECT_FALSE(paint.fill_canvas_background);
    EXPECT_FALSE(paint.draw_preview_geometry);
}

TEST(SceneViewportTest, GpuSceneRenderingFlagSelectsCanvasPaint)
{
    ros2::SceneViewport viewport;
    EXPECT_FALSE(viewport.gpu_scene_rendering());
    EXPECT_TRUE(viewport.canvas_paint().fill_canvas_background);

    viewport.set_gpu_scene_rendering(true);
    EXPECT_TRUE(viewport.gpu_scene_rendering());
    EXPECT_FALSE(viewport.canvas_paint().fill_canvas_background);
    EXPECT_FALSE(viewport.canvas_paint().opaque_window_bg);
    EXPECT_FALSE(viewport.canvas_paint().draw_preview_geometry);

    viewport.set_gpu_scene_rendering(false);
    EXPECT_TRUE(viewport.canvas_paint().fill_canvas_background);
}

#ifdef SPECTRA_SCENE_VIEWPORT_IMGUI_TESTS
namespace
{

struct CoverageProbe
{
    bool covered{false};          // an opaque ImGui triangle covers the canvas centre
    bool margin_covered{false};   // ... and the window padding left of the canvas
    bool canvas_valid{false};
};

bool triangle_contains(const ImVec2& p, const ImVec2& a, const ImVec2& b, const ImVec2& c)
{
    const auto cross = [](const ImVec2& o, const ImVec2& u, const ImVec2& v)
    { return (u.x - o.x) * (v.y - o.y) - (u.y - o.y) * (v.x - o.x); };
    const float d1      = cross(a, b, p);
    const float d2      = cross(b, c, p);
    const float d3      = cross(c, a, p);
    const bool  has_neg = d1 < 0.0f || d2 < 0.0f || d3 < 0.0f;
    const bool  has_pos = d1 > 0.0f || d2 > 0.0f || d3 > 0.0f;
    return !(has_neg && has_pos);
}

bool point_covered_by_opaque_triangle(const ImDrawData& data, const ImVec2& p)
{
    for (const ImDrawList* list : data.CmdLists)
    {
        const auto& vtx = list->VtxBuffer;
        for (const ImDrawCmd& cmd : list->CmdBuffer)
        {
            if (cmd.UserCallback != nullptr)
                continue;
            for (unsigned int i = 0; i + 2 < cmd.ElemCount; i += 3)
            {
                const ImDrawVert& a = vtx[cmd.VtxOffset + list->IdxBuffer[cmd.IdxOffset + i]];
                const ImDrawVert& b = vtx[cmd.VtxOffset + list->IdxBuffer[cmd.IdxOffset + i + 1]];
                const ImDrawVert& c = vtx[cmd.VtxOffset + list->IdxBuffer[cmd.IdxOffset + i + 2]];
                const auto        opaque = [](const ImDrawVert& v)
                { return ((v.col >> IM_COL32_A_SHIFT) & 0xFFu) == 0xFFu; };
                if (opaque(a) && opaque(b) && opaque(c)
                    && triangle_contains(p, a.pos, b.pos, c.pos))
                    return true;
            }
        }
    }
    return false;
}

// Draws one Scene Viewport frame with a single marker at the origin (which the
// software preview renders as a centre dot) and reports whether the canvas
// centre ends up covered by opaque ImGui geometry.
CoverageProbe probe_canvas_center(bool gpu_scene_rendering)
{
    spectra::ui::ThemeManager theme_manager;
    spectra::ui::ThemeManager::set_current(&theme_manager);
    theme_manager.ensure_initialized();

    IMGUI_CHECKVERSION();
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO&      io  = ImGui::GetIO();
    io.IniFilename    = nullptr;
    io.DisplaySize    = ImVec2(800.0f, 600.0f);
    io.DeltaTime      = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    ros2::SceneManager scene;
    ros2::SceneEntity  marker;
    marker.type  = "marker";
    marker.label = "cube";
    scene.add_entity(marker);

    ros2::SceneViewport viewport;
    viewport.set_gpu_scene_rendering(gpu_scene_rendering);

    CoverageProbe probe;
    // Two frames so the window has a stable size and layout.
    for (int frame = 0; frame < 2; ++frame)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f));
        viewport.draw(nullptr, "map", 1, scene);
        ImGui::Render();
    }

    const spectra::Rect rect = viewport.canvas_rect();
    probe.canvas_valid       = rect.w > 8.0f && rect.h > 8.0f;
    if (probe.canvas_valid)
    {
        const ImDrawData& data  = *ImGui::GetDrawData();
        const float       mid_y = rect.y + rect.h * 0.5f;
        probe.covered =
            point_covered_by_opaque_triangle(data, ImVec2(rect.x + rect.w * 0.5f, mid_y));
        probe.margin_covered = point_covered_by_opaque_triangle(data, ImVec2(rect.x - 2.0f, mid_y));
    }
    ImGui::DestroyContext(ctx);
    spectra::ui::ThemeManager::set_current(nullptr);
    return probe;
}

}   // namespace

TEST(SceneViewportTest, SoftwarePreviewCoversCanvasCenter)
{
    const CoverageProbe probe = probe_canvas_center(false);
    ASSERT_TRUE(probe.canvas_valid);
    EXPECT_TRUE(probe.covered);
    EXPECT_TRUE(probe.margin_covered);
}

TEST(SceneViewportTest, GpuSceneRenderingLeavesCanvasCenterUncovered)
{
    const CoverageProbe probe = probe_canvas_center(true);
    ASSERT_TRUE(probe.canvas_valid);
    EXPECT_FALSE(probe.covered);
    // The window background is still painted around the canvas.
    EXPECT_TRUE(probe.margin_covered);
}
#endif
