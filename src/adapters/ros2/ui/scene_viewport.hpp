#pragma once

#include <array>
#include <cstddef>
#include <string>

#include <spectra/camera.hpp>

#include "tools/measure_tool.hpp"

namespace spectra
{
struct Rect;
}

namespace spectra::adapters::ros2
{

class SceneManager;

// What the ImGui scene canvas paints.  When a GPU scene renderer draws the 3D
// content into the canvas rect (before ImGui), the canvas must stay transparent
// and only handle interaction; otherwise it draws the software preview.
struct SceneCanvasPaint
{
    bool opaque_window_bg{true};         // SceneViewport window WindowBg over the canvas
    bool fill_canvas_background{true};   // Background colour fill of the canvas rect
    bool draw_preview_geometry{true};    // Software preview of axes and entities
};

constexpr SceneCanvasPaint scene_canvas_paint(bool gpu_scene_rendering)
{
    if (gpu_scene_rendering)
        return {.opaque_window_bg       = false,
                .fill_canvas_background = false,
                .draw_preview_geometry  = false};
    return {};
}

class SceneViewport
{
   public:
    SceneViewport();

    void               set_title(const std::string& title) { title_ = title; }
    const std::string& title() const { return title_; }

    const spectra::Camera& camera() const { return camera_; }
    void                   set_camera(const spectra::Camera& camera);

    const std::array<float, 4>& background_rgba() const { return background_rgba_; }
    void set_background_rgba(const std::array<float, 4>& rgba) { background_rgba_ = rgba; }

    // Returns the screen-space canvas rect recorded during the last draw().
    // Used by the GPU scene render callback.
    spectra::Rect canvas_rect() const;
    void          invalidate_canvas_rect();

    // Set by the host when a GPU scene renderer draws this viewport's canvas
    // rect (spectra-ros installs one).  The software preview is the fallback.
    void             set_gpu_scene_rendering(bool enabled) { gpu_scene_rendering_ = enabled; }
    bool             gpu_scene_rendering() const { return gpu_scene_rendering_; }
    SceneCanvasPaint canvas_paint() const { return scene_canvas_paint(gpu_scene_rendering_); }

    void draw(bool*              p_open,
              const std::string& fixed_frame,
              size_t             display_count,
              SceneManager&      scene);

    MeasureTool&       measure_tool() { return measure_tool_; }
    const MeasureTool& measure_tool() const { return measure_tool_; }

   private:
    std::string          title_{"Scene Viewport"};
    spectra::Camera      camera_{};
    std::array<float, 4> background_rgba_{{0.08f, 0.09f, 0.12f, 1.0f}};
    bool                 camera_initialized_{false};
    bool                 gpu_scene_rendering_{false};

    // Screen-space canvas rect from last ImGui draw (x, y, w, h).
    float canvas_x_{0.0f};
    float canvas_y_{0.0f};
    float canvas_w_{0.0f};
    float canvas_h_{0.0f};

    MeasureTool measure_tool_;
};

}   // namespace spectra::adapters::ros2
