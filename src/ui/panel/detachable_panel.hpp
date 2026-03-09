#pragma once

// DetachablePanel — base class for ImGui panels that support detaching from
// the main dockspace into a standalone OS window (and re-attaching).
//
// Subclasses must implement:
//   - draw_content()   — render the panel body
//
// Usage:
//   1. Inherit from DetachablePanel.
//   2. Override draw_content() with ImGui rendering calls.
//   3. Call set_window_manager() with the App's WindowManager.
//   4. Call draw(&visible) each frame from the app shell.
//   5. Call process_pending() AFTER app.step() to safely create/destroy
//      panel OS windows (outside the session_runtime window iteration).
//
// When the user right-clicks the panel, a context menu offers
// "Detach to Window" (opens a real OS window) or "Attach to Dockspace"
// (closes the OS window and re-docks).  Closing the OS window via its
// title-bar close button also re-attaches.
//
// Thread-safety: draw() and process_pending() must be called from the
//                ImGui/render thread only.

#include <cstdint>
#include <string>

namespace spectra
{
class WindowManager;
}

namespace spectra::ui
{

class DetachablePanel
{
public:
    explicit DetachablePanel(const std::string& title = "Panel")
        : title_(title)
    {
    }

    virtual ~DetachablePanel();

    DetachablePanel(const DetachablePanel&)            = delete;
    DetachablePanel& operator=(const DetachablePanel&) = delete;
    DetachablePanel(DetachablePanel&&)                 = default;
    DetachablePanel& operator=(DetachablePanel&&)      = default;

    // -----------------------------------------------------------------------
    // Public interface
    // -----------------------------------------------------------------------

    /// Draw the panel (within ImGui frame of the main window).
    void draw(bool* p_open = nullptr);

    /// Process deferred OS window create/destroy.
    /// MUST be called AFTER app.step() — outside the session_runtime
    /// window iteration loop — to safely mutate the window list.
    void process_pending();

    /// Title management.
    const std::string& title() const { return title_; }
    void set_title(const std::string& t) { title_ = t; }

    /// Detach / attach control.
    bool is_detached() const { return panel_window_id_ != 0; }
    void detach();
    void attach();

    /// Initial size hint for when the panel is first detached (pixels).
    void set_detached_size(float w, float h)
    {
        detached_width_  = w;
        detached_height_ = h;
    }

    /// Dockspace ID to re-attach into (0 = default main dockspace).
    void set_dock_id(uint32_t id) { dock_id_ = id; }
    uint32_t dock_id() const { return dock_id_; }

    /// Set the WindowManager for OS-level panel tearoff.
    void set_window_manager(spectra::WindowManager* wm) { window_mgr_ = wm; }

protected:
    /// Render the panel body.  Called from both the main window context
    /// (when docked) and the panel OS window context (when detached).
    virtual void draw_content() = 0;

    /// Optional: extra window flags (combined with base flags).
    virtual int extra_window_flags() const { return 0; }

private:
    void draw_context_menu();

    std::string title_;
    bool        detached_{false};

    float    detached_width_{400.0f};
    float    detached_height_{300.0f};
    uint32_t dock_id_{0};

    // OS-level tearoff state.
    spectra::WindowManager* window_mgr_{nullptr};
    uint32_t                panel_window_id_{0};

    // Deferred operations — applied in process_pending() outside iteration.
    bool wants_create_window_{false};
    bool wants_destroy_window_{false};
    int  create_screen_x_{0};
    int  create_screen_y_{0};
};

}   // namespace spectra::ui
