#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <cstdint>
    #include <functional>
    #include <spectra/fwd.hpp>
    #include <string>

namespace spectra
{

class DockSystem;
class WindowManager;

// ─── TabDragController ───────────────────────────────────────────────────────
// Implements the drag state machine for tab tear-off (Phase 4 of the
// multi-window plan).  Manages the lifecycle of a tab drag from initial
// mouse-down through threshold detection, ghost overlay, drop resolution,
// and cancellation.
//
// State machine:
//
//   Idle ──mouse_down──► DragStartCandidate
//                              │
//                    move > threshold
//                              │
//                              ▼
//                       DraggingDetached ──ESC/right-click──► Cancel ──► Idle
//                         │          │
//                   mouse_up       mouse_up
//                   INSIDE         OUTSIDE
//                     │              │
//                     ▼              ▼
//                DropInside     DropOutside
//                     │              │
//                     └──────────────┘
//                            │
//                            ▼
//                          Idle
//
// Integration:
//   - ImGuiIntegration::draw_pane_tab_headers() calls on_mouse_down(),
//     update(), and the controller drives state transitions.
//   - Ghost overlay rendering is done by draw_ghost_tab() each frame
//     while in DraggingDetached state.
//   - Drop callbacks fire on state transitions to DropInside / DropOutside.

class TabDragController
{
   public:
    // ── State enum ──────────────────────────────────────────────────────

    enum class State
    {
        Idle,
        DragStartCandidate,
        DraggingDetached,
        // DropInside / DropOutside / Cancel are transient — they execute
        // their side-effects and immediately transition back to Idle.
    };

    // ── Callback types ──────────────────────────────────────────────────

    // Called when a drop occurs inside a window (dock/split behavior).
    // Parameters: figure_id, mouse_x, mouse_y (window-local coords).
    using DropInsideCallback =
        std::function<void(FigureId figure_id, float mouse_x, float mouse_y)>;

    // Called when a drop occurs outside all windows (spawn new window).
    // Parameters: figure_id, screen_x, screen_y.
    using DropOutsideCallback =
        std::function<void(FigureId figure_id, float screen_x, float screen_y)>;

    // Called when drag is cancelled (restore original state).
    using CancelCallback = std::function<void(FigureId figure_id)>;

    TabDragController() = default;
    ~TabDragController() = default;

    // Non-copyable
    TabDragController(const TabDragController&) = delete;
    TabDragController& operator=(const TabDragController&) = delete;

    // ── Configuration ───────────────────────────────────────────────────

    void set_dock_system(DockSystem* ds) { dock_system_ = ds; }
    void set_window_manager(WindowManager* wm) { window_manager_ = wm; }

    void set_on_drop_inside(DropInsideCallback cb) { on_drop_inside_ = std::move(cb); }
    void set_on_drop_outside(DropOutsideCallback cb) { on_drop_outside_ = std::move(cb); }
    void set_on_cancel(CancelCallback cb) { on_cancel_ = std::move(cb); }

    // Pixel threshold before a mouse-down becomes a drag (default: 10px).
    void set_drag_threshold(float px) { drag_threshold_ = px; }

    // Vertical pixel threshold before entering dock-drag mode (default: 30px).
    void set_dock_drag_threshold(float px) { dock_drag_threshold_ = px; }

    // ── Input events (called by ImGuiIntegration) ───────────────────────

    // Call when mouse is pressed on a tab header.
    // source_pane_id: the pane containing the tab.
    // figure_id: the figure being potentially dragged.
    // mouse_x, mouse_y: window-local mouse position at press time.
    void on_mouse_down(uint32_t source_pane_id, FigureId figure_id,
                       float mouse_x, float mouse_y);

    // Call every frame while mouse is held (or after release to finalize).
    // mouse_x, mouse_y: current window-local mouse position.
    // mouse_down: true if left mouse button is still held.
    // screen_mouse_x, screen_mouse_y: screen-space cursor position
    //   (used for outside-window detection).
    void update(float mouse_x, float mouse_y, bool mouse_down,
                float screen_mouse_x, float screen_mouse_y);

    // Call when ESC is pressed or right mouse button is clicked during drag.
    void cancel();

    // ── Queries ─────────────────────────────────────────────────────────

    State state() const { return state_; }
    bool is_dragging() const { return state_ == State::DraggingDetached; }
    bool is_active() const { return state_ != State::Idle; }

    FigureId dragged_figure() const { return figure_id_; }
    uint32_t source_pane_id() const { return source_pane_id_; }

    // Current mouse position (window-local) — valid during DraggingDetached.
    float mouse_x() const { return current_mouse_x_; }
    float mouse_y() const { return current_mouse_y_; }

    // True if the drag has moved outside the source pane header area.
    bool is_cross_pane() const { return cross_pane_; }
    void set_cross_pane(bool v) { cross_pane_ = v; }

    // True if vertical displacement exceeded dock_drag_threshold_.
    bool is_dock_dragging() const { return dock_dragging_; }

    // ── Ghost tab info (for rendering by ImGuiIntegration) ──────────────

    // Title to display on the ghost tab.
    void set_ghost_title(const std::string& title) { ghost_title_ = title; }
    const std::string& ghost_title() const { return ghost_title_; }

   private:
    void transition_to_idle();
    void transition_to_dragging();
    void execute_drop_inside(float mouse_x, float mouse_y);
    void execute_drop_outside(float screen_x, float screen_y);
    void execute_cancel();

    // Check if screen coordinates are outside all managed windows.
    bool is_outside_all_windows(float screen_x, float screen_y) const;

    // ── State ───────────────────────────────────────────────────────────

    State state_ = State::Idle;

    // Drag origin
    uint32_t source_pane_id_ = 0;
    FigureId figure_id_ = INVALID_FIGURE_ID;
    float start_mouse_x_ = 0.0f;
    float start_mouse_y_ = 0.0f;

    // Current position
    float current_mouse_x_ = 0.0f;
    float current_mouse_y_ = 0.0f;
    float current_screen_x_ = 0.0f;
    float current_screen_y_ = 0.0f;

    // Sub-states
    bool cross_pane_ = false;
    bool dock_dragging_ = false;

    // Ghost tab
    std::string ghost_title_;

    // Thresholds
    float drag_threshold_ = 10.0f;
    float dock_drag_threshold_ = 30.0f;

    // External references (not owned)
    DockSystem* dock_system_ = nullptr;
    WindowManager* window_manager_ = nullptr;

    // Callbacks
    DropInsideCallback on_drop_inside_;
    DropOutsideCallback on_drop_outside_;
    CancelCallback on_cancel_;
};

}  // namespace spectra

#endif  // SPECTRA_USE_IMGUI
