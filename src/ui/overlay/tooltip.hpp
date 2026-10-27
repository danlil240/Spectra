#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <spectra/color.hpp>
    #include <spectra/series.hpp>
    #include <string>

struct ImDrawList;
struct ImFont;

namespace spectra::ui
{
class ThemeManager;
}   // namespace spectra::ui

namespace spectra
{

class Axes3D;

// Result of nearest-point spatial query
struct NearestPointResult
{
    bool          found        = false;
    const Series* series       = nullptr;
    int           series_index = -1;   // 0-based index within its axes (-1 = unknown)
    size_t        point_index  = 0;
    // Absolute data x/y (include the series x_offset/y_offset); double so
    // epoch-scale timestamps and large offsets keep their precision.
    double        data_x      = 0.0;
    double        data_y      = 0.0;
    float         data_z      = 0.0f;
    float         screen_x    = 0.0f;
    float         screen_y    = 0.0f;
    float         distance_px = 0.0f;
    float         ndc_depth   = 1.0f;    // Clip-space depth for 3D tie-breaking
    float         dy_dx       = 0.0f;    // Finite-difference derivative at the point
    bool          dy_dx_valid = false;   // True when derivative could be computed
    bool          is_3d       = false;
    const Axes3D* axes3d      = nullptr;   // Owning 3D axes (for axis labels)
};

// Display name for the hovered series: its label, or "Series N" (1-based,
// matching the order the series were added to the axes) when unlabeled.
inline std::string tooltip_series_name(const NearestPointResult& nearest)
{
    if (nearest.series && !nearest.series->label().empty())
        return nearest.series->label();
    if (nearest.series_index >= 0)
        return "Series " + std::to_string(nearest.series_index + 1);
    return "Series";
}

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
    // When dl is non-null, draws into the given draw list instead of GetForegroundDrawList().
    void draw(const NearestPointResult& nearest,
              float                     window_width,
              float                     window_height,
              ImDrawList*               dl = nullptr);

    // Configuration
    void  set_snap_radius(float px) { snap_radius_px_ = px; }
    float snap_radius() const { return snap_radius_px_; }

    void  set_snap_radius_3d(float px) { snap_radius_3d_px_ = px; }
    float snap_radius_3d() const { return snap_radius_3d_px_; }

    void set_enabled(bool e) { enabled_ = e; }
    bool enabled() const { return enabled_; }
    void set_theme_manager(ui::ThemeManager* tm) { theme_mgr_ = tm; }

   private:
    ui::ThemeManager* theme_mgr_         = nullptr;
    ImFont*           font_body_         = nullptr;
    ImFont*           font_heading_      = nullptr;
    float             snap_radius_px_    = 8.0f;
    float             snap_radius_3d_px_ = 16.0f;
    bool              enabled_           = true;

    void draw_3d(const NearestPointResult& nearest,
                 float                     window_width,
                 float                     window_height,
                 ImDrawList*               dl);

    // Animation state
    float opacity_        = 0.0f;
    float target_opacity_ = 0.0f;
    float hysteresis_     = 0.0f;   // Time since cursor left snap radius

    // Fade out immediately when the snap point is lost (cursor left plot/window).
    void fade_out_if_no_snap(const NearestPointResult& nearest, float dt);
};

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
