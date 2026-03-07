#pragma once

#include <array>
#include <cstddef>
#include <string>

#include <spectra/camera.hpp>

namespace spectra
{
struct Rect;
}

namespace spectra::adapters::ros2
{

class SceneManager;

class SceneViewport
{
public:
    SceneViewport();

    void set_title(const std::string& title) { title_ = title; }
    const std::string& title() const { return title_; }

    const spectra::Camera& camera() const { return camera_; }
    void set_camera(const spectra::Camera& camera);

    const std::array<float, 4>& background_rgba() const { return background_rgba_; }
    void set_background_rgba(const std::array<float, 4>& rgba) { background_rgba_ = rgba; }

    // Returns the screen-space canvas rect recorded during the last draw().
    // Used by the GPU scene render callback.
    spectra::Rect canvas_rect() const;

    void draw(bool* p_open,
              const std::string& fixed_frame,
              size_t display_count,
              SceneManager& scene);

private:
    std::string title_{"Scene Viewport"};
    spectra::Camera camera_{};
    std::array<float, 4> background_rgba_{{0.08f, 0.09f, 0.12f, 1.0f}};
    bool camera_initialized_{false};

    // Screen-space canvas rect from last ImGui draw (x, y, w, h).
    float canvas_x_{0.0f};
    float canvas_y_{0.0f};
    float canvas_w_{0.0f};
    float canvas_h_{0.0f};
};

}   // namespace spectra::adapters::ros2
