#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace spectra
{

namespace ui
{
struct ThemeColors;
}

// Context passed to overlay draw callbacks.
// Provides viewport geometry, mouse state, and figure identification.
struct OverlayDrawContext
{
    float    viewport_x;
    float    viewport_y;
    float    viewport_w;
    float    viewport_h;
    float    mouse_x;
    float    mouse_y;
    bool     is_hovered;
    uint32_t figure_id;
    int      axes_index;
    int      series_count;
    void*    draw_list;   // opaque ImDrawList* for internal use

    // Appearance for overlays that should follow the host UI.
    const ui::ThemeColors* theme     = nullptr;   // current theme colours (not owned)
    float                  ui_scale  = 1.0f;      // UI scale factor (1.0 = 100%)
    float                  font_size = 0.0f;      // UI font height in pixels; 0 = unknown
};

// Registry for plugin-driven draw overlays.
// Thread-safe for registration; draw_all() must be called from the UI thread.
class OverlayRegistry
{
   public:
    using DrawCallback = std::function<void(const OverlayDrawContext& ctx)>;

    OverlayRegistry()  = default;
    ~OverlayRegistry() = default;

    OverlayRegistry(const OverlayRegistry&)            = delete;
    OverlayRegistry& operator=(const OverlayRegistry&) = delete;

    // Register an overlay with a unique name.
    void register_overlay(const std::string& name, DrawCallback callback);

    // Remove a previously registered overlay.
    void unregister_overlay(const std::string& name);

    // Invoke all registered overlay callbacks for the given context.
    // Must be called from the UI/render thread.
    void draw_all(const OverlayDrawContext& ctx) const;

    // Query registered overlay names.
    std::vector<std::string> overlay_names() const;

    // Number of registered overlays.
    size_t count() const;

   private:
    struct Entry
    {
        std::string  name;
        DrawCallback callback;
        bool         faulted = false;   // Set true on crash/exception to skip future invocations
    };

    mutable std::mutex         mutex_;
    mutable std::vector<Entry> overlays_;
};

}   // namespace spectra
