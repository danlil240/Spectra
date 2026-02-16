#pragma once

#ifdef PLOTIX_USE_IMGUI

    #include <plotix/color.hpp>

    #include "input.hpp"

namespace plotix
{

class TransitionEngine;

// Visual overlay for box zoom selection rectangle.
// Renders a semi-transparent filled rectangle with dashed border,
// corner handles, and dimension labels during box zoom drag.
// Integrates with TransitionEngine for animated appearance/disappearance.
class BoxZoomOverlay
{
   public:
    BoxZoomOverlay() = default;

    // Set the transition engine for animated opacity (optional, graceful fallback)
    void set_transition_engine(TransitionEngine* te) { transition_engine_ = te; }

    // Set the input handler to read box zoom state from
    void set_input_handler(const InputHandler* ih) { input_handler_ = ih; }

    // Per-frame update: reads box zoom rect from InputHandler, manages animation state.
    // dt in seconds.
    void update(float dt);

    // Draw the overlay. Call inside ImGui frame, after canvas rendering.
    // window_width/height are the full window dimensions.
    void draw(float window_width, float window_height);

    // Query state
    bool is_active() const { return active_; }
    float opacity() const { return opacity_; }

    // Configuration
    void set_fill_opacity(float a) { fill_opacity_ = a; }
    void set_border_width(float w) { border_width_ = w; }
    void set_dash_length(float l) { dash_length_ = l; }
    void set_dash_gap(float g) { dash_gap_ = g; }
    void set_show_dimensions(bool s) { show_dimensions_ = s; }
    void set_show_crosshair(bool s) { show_crosshair_ = s; }

    float fill_opacity() const { return fill_opacity_; }
    float border_width() const { return border_width_; }
    float dash_length() const { return dash_length_; }
    float dash_gap() const { return dash_gap_; }
    bool show_dimensions() const { return show_dimensions_; }
    bool show_crosshair() const { return show_crosshair_; }

   private:
    // Internal drawing helpers (implemented in .cpp with ImGui access)
    void draw_dashed_line_impl(
        float x0, float y0, float x1, float y1, unsigned int col, float thickness) const;
    void draw_corner_handles_impl(float x0, float y0, float x1, float y1, unsigned int col) const;
    void draw_dimension_label_impl(float x0, float y0, float x1, float y1, unsigned int col) const;
    void draw_zoom_crosshair_impl(float x0,
                                  float y0,
                                  float x1,
                                  float y1,
                                  float vp_x,
                                  float vp_y,
                                  float vp_w,
                                  float vp_h,
                                  unsigned int col) const;

    const InputHandler* input_handler_ = nullptr;
    TransitionEngine* transition_engine_ = nullptr;

    // Current state
    bool active_ = false;
    float opacity_ = 0.0f;  // 0..1, animated

    // Cached rect (screen coords)
    float rect_x0_ = 0.0f, rect_y0_ = 0.0f;
    float rect_x1_ = 0.0f, rect_y1_ = 0.0f;

    // Configuration
    float fill_opacity_ = 0.12f;   // Fill alpha multiplier
    float border_width_ = 1.5f;    // Border line thickness
    float dash_length_ = 6.0f;     // Dash segment length
    float dash_gap_ = 4.0f;        // Gap between dashes
    bool show_dimensions_ = true;  // Show W×H label
    bool show_crosshair_ = true;   // Show extending crosshair lines

    // Animation
    static constexpr float FADE_IN_SPEED = 12.0f;  // Exponential lerp rate
    static constexpr float FADE_OUT_SPEED = 8.0f;
    static constexpr float CORNER_HANDLE_SIZE = 4.0f;
};

}  // namespace plotix

#endif  // PLOTIX_USE_IMGUI
