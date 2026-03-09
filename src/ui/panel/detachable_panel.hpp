#pragma once

// DetachablePanel — base class for ImGui panels that support detaching from
// the main dockspace into a standalone floating window (and re-attaching).
//
// Subclasses must implement:
//   - draw_content()   — render the panel body (called between Begin/End)
//   - default_title()  — return the default window title
//
// Usage:
//   1. Inherit from DetachablePanel.
//   2. Override draw_content() with ImGui rendering calls.
//   3. Call draw(&visible) each frame from the app shell.
//
// When the user right-clicks the panel title bar, a context menu offers
// "Detach" (pops the panel to a floating window) or "Attach" (returns it
// to the dockspace).  The panel can also be dragged out of the dockspace
// using standard ImGui docking drag-and-drop.
//
// Thread-safety: draw() must be called from the ImGui render thread only.

#include <cstdint>
#include <string>

namespace spectra::ui
{

class DetachablePanel
{
public:
    explicit DetachablePanel(const std::string& title = "Panel")
        : title_(title)
    {
    }

    virtual ~DetachablePanel() = default;

    DetachablePanel(const DetachablePanel&)            = delete;
    DetachablePanel& operator=(const DetachablePanel&) = delete;
    DetachablePanel(DetachablePanel&&)                 = default;
    DetachablePanel& operator=(DetachablePanel&&)      = default;

    // -----------------------------------------------------------------------
    // Public interface
    // -----------------------------------------------------------------------

    /// Draw the panel.  Pass a visibility flag (nullptr = always visible).
    void draw(bool* p_open = nullptr);

    /// Title management.
    const std::string& title() const { return title_; }
    void set_title(const std::string& t) { title_ = t; }

    /// Detach / attach control.
    bool is_detached() const { return detached_; }
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

protected:
    // -----------------------------------------------------------------------
    // Override points
    // -----------------------------------------------------------------------

    /// Render the panel body.  Called inside ImGui::Begin()/End() scope.
    virtual void draw_content() = 0;

    /// Optional: extra window flags (combined with base flags).
    virtual int extra_window_flags() const { return 0; }

private:
    void draw_context_menu();

    std::string title_;
    bool        detached_{false};

    // When true, the next draw() call will undock the window.
    bool pending_detach_{false};
    // When true, the next draw() call will dock the window back.
    bool pending_attach_{false};

    float    detached_width_{400.0f};
    float    detached_height_{300.0f};
    uint32_t dock_id_{0};
};

}   // namespace spectra::ui
