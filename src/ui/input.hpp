#pragma once

#include <plotix/axes.hpp>
#include <plotix/axes3d.hpp>
#include <plotix/figure.hpp>

#include <chrono>
#include <functional>

namespace plotix {

class AnimationController;
class AxisLinkManager;
class DataInteraction;
class GestureRecognizer;
class ShortcutManager;
class TransitionEngine;

// Tool mode (selected by toolbar buttons)
enum class ToolMode {
    Pan,       // Pan tool - left click and drag to pan
    BoxZoom,   // Box zoom tool - right click and drag to zoom
    Select,    // Select tool - left click and drag to region-select data points
};

// Interaction state for the input handler (dragging state)
enum class InteractionMode {
    Idle,
    Dragging,  // Currently dragging (either pan or box zoom)
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
    InputHandler();
    ~InputHandler();

    // Set the figure for multi-axes hit-testing
    void set_figure(Figure* fig) { figure_ = fig; }

    // Set the active axes that input events will affect
    void set_active_axes(Axes* axes) { active_axes_ = axes; active_axes_base_ = axes; }
    void set_active_axes_base(AxesBase* axes) { active_axes_base_ = axes; active_axes_ = dynamic_cast<Axes*>(axes); }
    Axes* active_axes() const { return active_axes_; }
    AxesBase* active_axes_base() const { return active_axes_base_; }

    // Mouse button event: begin/end drag or box zoom
    void on_mouse_button(int button, int action, int mods, double x, double y);

    // Mouse move event: pan if dragging, update cursor readout
    void on_mouse_move(double x, double y);

    // Scroll event: zoom around cursor position (animated)
    void on_scroll(double x_offset, double y_offset, double cursor_x, double cursor_y);

    // Per-frame update: advances animations and inertial pan. dt in seconds.
    void update(float dt);

    // Set the animation controller (owned externally, e.g. by App)
    void set_animation_controller(AnimationController* ctrl) { anim_ctrl_ = ctrl; }
    AnimationController* animation_controller() const { return anim_ctrl_; }

    // Set the gesture recognizer (owned externally)
    void set_gesture_recognizer(GestureRecognizer* gr) { gesture_ = gr; }
    GestureRecognizer* gesture_recognizer() const { return gesture_; }

    // Set the data interaction layer (owned externally)
    void set_data_interaction(DataInteraction* di) { data_interaction_ = di; }
    DataInteraction* data_interaction() const { return data_interaction_; }

    // Set the transition engine (owned externally by App)
    // When set, preferred over AnimationController for all animations.
    void set_transition_engine(TransitionEngine* te) { transition_engine_ = te; }
    TransitionEngine* transition_engine() const { return transition_engine_; }

    // Set the shortcut manager (owned externally by App)
    void set_shortcut_manager(ShortcutManager* sm) { shortcut_mgr_ = sm; }
    ShortcutManager* shortcut_manager() const { return shortcut_mgr_; }

    // Set the axis link manager (owned externally by App)
    void set_axis_link_manager(AxisLinkManager* alm) { axis_link_mgr_ = alm; }
    AxisLinkManager* axis_link_manager() const { return axis_link_mgr_; }

    // Lock/unlock orbit rotation (lock in 2D mode so drag = pan only)
    void set_orbit_locked(bool locked) { orbit_locked_ = locked; }
    bool orbit_locked() const { return orbit_locked_; }

    // Key event: keyboard shortcuts
    void on_key(int key, int action, int mods);

    // Set the viewport rect for coordinate mapping (single-axes fallback)
    void set_viewport(float vp_x, float vp_y, float vp_w, float vp_h);

    // Current interaction state (dragging)
    InteractionMode mode() const { return mode_; }
    void set_mode(InteractionMode new_mode) { mode_ = new_mode; }
    
    // Current tool mode (selected by toolbar)
    ToolMode tool_mode() const { return tool_mode_; }
    void set_tool_mode(ToolMode new_tool) { tool_mode_ = new_tool; }

    // Cursor readout (for overlay rendering)
    const CursorReadout& cursor_readout() const { return cursor_readout_; }

    // Box zoom rectangle (for overlay rendering)
    const BoxZoomRect& box_zoom_rect() const { return box_zoom_; }

    // True if any interaction animation is running (zoom, pan inertia, auto-fit)
    bool has_active_animations() const;

    // Register callback for Ctrl+S save
    void set_save_callback(SavePngCallback cb) { save_callback_ = std::move(cb); }

    // Convert screen coordinates to data coordinates using current active axes
    void screen_to_data(double screen_x, double screen_y,
                        float& data_x, float& data_y) const;

    // Hit-test: find which Axes the cursor is over (public for context menu)
    Axes* hit_test_axes(double screen_x, double screen_y) const;

    // Hit-test all axes (including 3D) — returns AxesBase*
    AxesBase* hit_test_all_axes(double screen_x, double screen_y) const;

private:
    // Get viewport for a given axes (from figure layout)
    const Rect& viewport_for_axes(const AxesBase* axes) const;

    // Apply box zoom: set limits from selection rectangle
    void apply_box_zoom();

    // Cancel box zoom
    void cancel_box_zoom();

    Figure* figure_       = nullptr;
    Axes*   active_axes_  = nullptr;
    AxesBase* active_axes_base_ = nullptr;

    // 3D orbit drag state
    bool is_3d_orbit_drag_ = false;
    bool is_3d_pan_drag_ = false;
    bool orbit_locked_ = false;  // When true, orbit drag becomes pan (2D mode)
    static constexpr int MOUSE_BUTTON_MIDDLE = 2;
    static constexpr float ORBIT_SENSITIVITY = 0.3f;
    static constexpr float ZOOM_3D_FACTOR = 0.1f;

    InteractionMode mode_ = InteractionMode::Idle;
    ToolMode tool_mode_ = ToolMode::Pan;  // Default tool mode

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

    // Animation controller (not owned)
    AnimationController* anim_ctrl_ = nullptr;

    // Gesture recognizer (not owned)
    GestureRecognizer* gesture_ = nullptr;

    // Data interaction layer (not owned)
    DataInteraction* data_interaction_ = nullptr;

    // Transition engine (not owned) — preferred over anim_ctrl_ when available
    TransitionEngine* transition_engine_ = nullptr;

    // Shortcut manager (not owned)
    ShortcutManager* shortcut_mgr_ = nullptr;

    // Axis link manager (not owned)
    AxisLinkManager* axis_link_mgr_ = nullptr;

    // Modifier key tracking (updated from on_key)
    int mods_ = 0;

    // Region selection drag state
    bool region_dragging_ = false;

    // Ctrl+drag box zoom state (allows box zoom in Pan mode via modifier)
    bool ctrl_box_zoom_active_ = false;

    // Inertial pan tracking: velocity in screen px/sec at drag release
    double last_move_x_ = 0.0;
    double last_move_y_ = 0.0;
    using Clock = std::chrono::steady_clock;
    Clock::time_point last_move_time_{};
    Clock::time_point drag_start_time_{};

    // Zoom factor per scroll tick (0.25 = 25% range change per tick)
    static constexpr float ZOOM_FACTOR = 0.25f;

    // Animated zoom duration (seconds)
    static constexpr float ZOOM_ANIM_DURATION = 0.15f;

    // Inertial pan duration (seconds)
    static constexpr float PAN_INERTIA_DURATION = 0.3f;

    // Auto-fit animation duration (seconds)
    static constexpr float AUTOFIT_ANIM_DURATION = 0.25f;

    // Minimum velocity (data-space units/sec) to trigger inertial pan
    static constexpr float MIN_INERTIA_VELOCITY = 0.01f;

    // GLFW key constants (to avoid including GLFW in header)
    static constexpr int KEY_R      = 82;
    static constexpr int KEY_G      = 71;
    static constexpr int KEY_S      = 83;
    static constexpr int KEY_C      = 67;
    static constexpr int KEY_A      = 65;
    static constexpr int KEY_ESCAPE = 256;
    static constexpr int MOD_SHIFT   = 0x0001;
    static constexpr int MOD_CONTROL = 0x0002;
};

} // namespace plotix
