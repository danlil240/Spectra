#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <cstdint>
    #include <functional>
    #include <string>

namespace spectra
{

class WindowManager;

// ─── PanelDetachController ───────────────────────────────────────────────────
// Pure state-machine controller for panel detach/attach lifecycle.
// Follows the same pattern as TabDragController: zero ImGui dependency,
// zero rendering — just state tracking, WindowManager interaction, and
// callbacks.  The caller handles all ImGui rendering.
//
// State machine:
//
//   Docked ──detach()──► DetachPending
//                              │
//                   process_pending() creates OS window
//                              │
//                              ▼
//                          Detached
//                         │          │
//                    attach()    OS window closed
//                         │          │
//                         ▼          ▼
//                      AttachPending
//                              │
//               process_pending() destroys OS window
//                              │
//                              ▼
//                           Docked
//
// Integration:
//   - The caller owns the ImGui Begin/End, context menus, and docking.
//   - The caller queries state() to decide what to render.
//   - The caller sets draw_callback so the OS window can render content.
//   - process_pending() is called AFTER app.step() to safely mutate
//     the window list outside session_runtime iteration.

class PanelDetachController
{
   public:
    // ── State enum ──────────────────────────────────────────────────────

    enum class State
    {
        Docked,          // Panel renders inside the host dockspace.
        DetachPending,   // Detach requested; OS window created next process_pending().
        Detached,        // Panel renders in its own OS window.
        AttachPending,   // Attach requested; OS window destroyed next process_pending().
    };

    // ── Callback types ──────────────────────────────────────────────────

    // Renders the panel body inside the OS window.
    using DrawCallback = std::function<void()>;

    // Notified after the panel has been detached (OS window created).
    using DetachedCallback = std::function<void(uint32_t window_id)>;

    // Notified after the panel has been re-attached (OS window destroyed).
    using AttachedCallback = std::function<void()>;

    PanelDetachController()  = default;
    ~PanelDetachController() = default;

    // Non-copyable.
    PanelDetachController(const PanelDetachController&)            = delete;
    PanelDetachController& operator=(const PanelDetachController&) = delete;

    // ── Configuration ───────────────────────────────────────────────────

    void set_window_manager(WindowManager* wm) { window_mgr_ = wm; }
    void set_draw_callback(DrawCallback cb) { draw_callback_ = std::move(cb); }
    void set_on_detached(DetachedCallback cb) { on_detached_ = std::move(cb); }
    void set_on_attached(AttachedCallback cb) { on_attached_ = std::move(cb); }

    void               set_title(const std::string& t) { title_ = t; }
    const std::string& title() const { return title_; }

    /// Initial size hint for the detached OS window (pixels).
    void set_detached_size(float w, float h)
    {
        detached_width_  = w;
        detached_height_ = h;
    }

    /// Screen position for window creation (set by caller before detach).
    void set_screen_position(int x, int y)
    {
        screen_x_ = x;
        screen_y_ = y;
    }

    /// Dockspace ID for re-attach (stored for the caller to query).
    void     set_dock_id(uint32_t id) { dock_id_ = id; }
    uint32_t dock_id() const { return dock_id_; }

    /// Get cursor position in screen coordinates (via WindowManager / GLFW).
    /// Same pattern as TabDragController::get_screen_cursor.
    bool get_screen_cursor(double& sx, double& sy) const;

    // ── Actions ─────────────────────────────────────────────────────────

    /// Request detach — queues OS window creation for next process_pending().
    void detach();

    /// Request attach — queues OS window destruction for next process_pending().
    void attach();

    // ── Frame update ────────────────────────────────────────────────────

    /// Check whether the OS window was closed externally (user clicked X).
    /// Call once per frame from the host window's frame loop.
    /// If the window was closed, transitions to AttachPending.
    void update();

    /// Process deferred OS window create/destroy.
    /// MUST be called AFTER app.step() — outside the session_runtime
    /// window iteration loop — to safely mutate the window list.
    void process_pending();

    // ── Queries ─────────────────────────────────────────────────────────

    State    state() const { return state_; }
    bool     is_detached() const { return state_ == State::Detached; }
    bool     is_docked() const { return state_ == State::Docked; }
    uint32_t panel_window_id() const { return panel_window_id_; }

   private:
    // State transitions.
    void transition_to_docked();
    void transition_to_detached(uint32_t window_id);
    void execute_create_window();
    void execute_destroy_window();

    // Check if the panel OS window is still alive.
    bool is_window_alive() const;

    // ── State ───────────────────────────────────────────────────────────

    State state_ = State::Docked;

    // Panel identity.
    std::string title_{"Panel"};
    uint32_t    dock_id_{0};

    // Detached window sizing.
    float detached_width_{400.0f};
    float detached_height_{300.0f};

    // Screen position for window creation.
    int screen_x_{100};
    int screen_y_{100};

    // OS window tracking.
    uint32_t panel_window_id_{0};

    // External references (not owned).
    WindowManager* window_mgr_{nullptr};

    // Callbacks.
    DrawCallback     draw_callback_;
    DetachedCallback on_detached_;
    AttachedCallback on_attached_;
};

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
