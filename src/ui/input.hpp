#pragma once

#include <plotix/axes.hpp>
#include <plotix/figure.hpp>

#include <functional>

namespace plotix {

// Interaction mode for the input handler
enum class InteractionMode {
    Idle,
    Pan,       // Left-drag: pan the view
    BoxZoom,   // Right-drag: select a region to zoom into
};

// Cursor readout data (updated every mouse move)
struct CursorReadout {
    bool   valid    = false;
    float  data_x   = 0.0f;
    float  data_y   = 0.0f;
    double screen_x = 0.0;
    double screen_y = 0.0;
};

// Box zoom selection rectangle (screen coordinates)
struct BoxZoomRect {
    bool   active = false;
    double x0     = 0.0;
    double y0     = 0.0;
    double x1     = 0.0;
    double y1     = 0.0;
};

// Callback for save-PNG shortcut (Ctrl+S)
using SavePngCallback = std::function<void()>;

// Input handler: maps mouse/keyboard events to axis limit mutations,
// box zoom, keyboard shortcuts, and multi-axes hit-testing.
class InputHandler {
public:
    InputHandler() = default;

    // Set the figure for multi-axes hit-testing
    void set_figure(Figure* fig) { figure_ = fig; }

    // Set the active axes that input events will affect
    void set_active_axes(Axes* axes) { active_axes_ = axes; }
    Axes* active_axes() const { return active_axes_; }

    // Mouse button event: begin/end drag or box zoom
    void on_mouse_button(int button, int action, double x, double y);

    // Mouse move event: pan if dragging, update cursor readout
    void on_mouse_move(double x, double y);

    // Scroll event: zoom around cursor position
    void on_scroll(double x_offset, double y_offset, double cursor_x, double cursor_y);

    // Key event: keyboard shortcuts
    void on_key(int key, int action, int mods);

    // Set the viewport rect for coordinate mapping (single-axes fallback)
    void set_viewport(float vp_x, float vp_y, float vp_w, float vp_h);

    // Current interaction mode
    InteractionMode mode() const { return mode_; }

    // Cursor readout (for overlay rendering)
    const CursorReadout& cursor_readout() const { return cursor_readout_; }

    // Box zoom rectangle (for overlay rendering)
    const BoxZoomRect& box_zoom_rect() const { return box_zoom_; }

    // Register callback for Ctrl+S save
    void set_save_callback(SavePngCallback cb) { save_callback_ = std::move(cb); }

    // Convert screen coordinates to data coordinates using current active axes
    void screen_to_data(double screen_x, double screen_y,
                        float& data_x, float& data_y) const;

private:
    // Hit-test: find which Axes the cursor is over
    Axes* hit_test_axes(double screen_x, double screen_y) const;

    // Get viewport for a given axes (from figure layout)
    const Rect& viewport_for_axes(const Axes* axes) const;

    // Apply box zoom: set limits from selection rectangle
    void apply_box_zoom();

    // Cancel box zoom
    void cancel_box_zoom();

    Figure* figure_       = nullptr;
    Axes*   active_axes_  = nullptr;

    InteractionMode mode_ = InteractionMode::Idle;

    // Pan drag state
    double drag_start_x_ = 0.0;
    double drag_start_y_ = 0.0;
    float  drag_start_xlim_min_ = 0.0f;
    float  drag_start_xlim_max_ = 0.0f;
    float  drag_start_ylim_min_ = 0.0f;
    float  drag_start_ylim_max_ = 0.0f;

    // Box zoom state
    BoxZoomRect box_zoom_;

    // Cursor readout
    CursorReadout cursor_readout_;

    // Viewport for coordinate mapping (single-axes fallback)
    float vp_x_ = 0.0f;
    float vp_y_ = 0.0f;
    float vp_w_ = 1.0f;
    float vp_h_ = 1.0f;

    // Callbacks
    SavePngCallback save_callback_;

    // Zoom factor per scroll tick
    static constexpr float ZOOM_FACTOR = 0.1f;

    // GLFW key constants (to avoid including GLFW in header)
    static constexpr int KEY_R      = 82;
    static constexpr int KEY_G      = 71;
    static constexpr int KEY_S      = 83;
    static constexpr int KEY_A      = 65;
    static constexpr int KEY_ESCAPE = 256;
    static constexpr int MOD_CONTROL = 0x0002;
};

} // namespace plotix
