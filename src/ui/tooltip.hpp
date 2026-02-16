#pragma once

#ifdef PLOTIX_USE_IMGUI

    #include <plotix/color.hpp>
    #include <plotix/series.hpp>

struct ImFont;

namespace plotix
{

// Result of nearest-point spatial query
struct NearestPointResult
{
    bool found = false;
    const Series* series = nullptr;
    size_t point_index = 0;
    float data_x = 0.0f;
    float data_y = 0.0f;
    float screen_x = 0.0f;
    float screen_y = 0.0f;
    float distance_px = 0.0f;
};

// Rich hover tooltip rendered via ImGui over the plot canvas.
// Shows series name, coordinates, and a color swatch.
class Tooltip
{
   public:
    Tooltip() = default;

    // Set fonts used for tooltip rendering
    void set_fonts(ImFont* body, ImFont* heading);

    // Draw the tooltip at the given screen position for the given nearest-point result.
    // Call inside an ImGui frame, after build_ui but before ImGui::Render().
    void draw(const NearestPointResult& nearest, float window_width, float window_height);

    // Configuration
    void set_snap_radius(float px) { snap_radius_px_ = px; }
    float snap_radius() const { return snap_radius_px_; }

    void set_enabled(bool e) { enabled_ = e; }
    bool enabled() const { return enabled_; }

   private:
    ImFont* font_body_ = nullptr;
    ImFont* font_heading_ = nullptr;
    float snap_radius_px_ = 8.0f;
    bool enabled_ = true;

    // Animation state
    float opacity_ = 0.0f;
    float target_opacity_ = 0.0f;
};

}  // namespace plotix

#endif  // PLOTIX_USE_IMGUI
