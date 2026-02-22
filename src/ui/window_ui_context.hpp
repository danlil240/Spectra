#pragma once

#include <algorithm>
#include <chrono>
#include <memory>
#include <spectra/fwd.hpp>
#include <unordered_map>

#ifdef SPECTRA_USE_IMGUI
    #include <spectra/camera.hpp>

    #include "animation_curve_editor.hpp"
    #include "axis_link.hpp"
    #include "box_zoom_overlay.hpp"
    #include "command_palette.hpp"
    #include "command_registry.hpp"
    #include "data_interaction.hpp"
    #include "dock_system.hpp"
    #include "figure_manager.hpp"
    #include "imgui_integration.hpp"
    #include "keyframe_interpolator.hpp"
    #include "knob_manager.hpp"
    #include "mode_transition.hpp"
    #include "shortcut_manager.hpp"
    #include "tab_bar.hpp"
    #include "tab_drag_controller.hpp"
    #include "timeline_editor.hpp"
    #include "undo_manager.hpp"
#endif

#ifdef SPECTRA_USE_GLFW
    #include "animation_controller.hpp"
    #include "gesture_recognizer.hpp"
    #include "input.hpp"
#endif

namespace spectra
{

// Per-window UI subsystem bundle.
// Groups all UI objects that were previously stack-local in App::run()
// so they can be instantiated per-window in multi-window mode.
//
// Phase 1 (PR1): Single instance used by App::run() — zero behavior change.
// Phase 2+: One instance per WindowContext for full multi-window support.
struct WindowUIContext
{
#ifdef SPECTRA_USE_IMGUI
    std::unique_ptr<ImGuiIntegration> imgui_ui;
    std::unique_ptr<DataInteraction> data_interaction;
    std::unique_ptr<TabBar> figure_tabs;

    BoxZoomOverlay box_zoom_overlay;

    FigureManager* fig_mgr = nullptr;  // Owned below via unique_ptr
    std::unique_ptr<FigureManager> fig_mgr_owned;

    DockSystem dock_system;
    bool dock_tab_sync_guard = false;

    AxisLinkManager axis_link_mgr;

    TimelineEditor timeline_editor;
    KeyframeInterpolator keyframe_interpolator;
    AnimationCurveEditor curve_editor;

    ModeTransition mode_transition;
    bool is_in_3d_mode = true;
    Camera saved_3d_camera;

    // Initial axes limits for Home button (restore original view)
    struct InitialLimits
    {
        AxisLimits x, y;
    };
    std::unordered_map<Axes*, InitialLimits> home_limits;

    CommandRegistry cmd_registry;
    ShortcutManager shortcut_mgr;
    UndoManager undo_mgr;
    CommandPalette cmd_palette;

    TabDragController tab_drag_controller;

    KnobManager knob_manager;

    // Cached data range for zoom level computation.
    // Avoids scanning all series x_data with minmax_element every frame.
    float cached_data_min = 0.0f;
    float cached_data_max = 0.0f;
    size_t cached_zoom_series_count = 0;
    bool zoom_cache_valid = false;
#endif

#ifdef SPECTRA_USE_GLFW
    AnimationController anim_controller;
    GestureRecognizer gesture;
    InputHandler input_handler;

    bool needs_resize = false;
    uint32_t new_width = 0;
    uint32_t new_height = 0;
    std::chrono::steady_clock::time_point resize_requested_time;
#endif

    // Non-copyable, non-movable (contains unique_ptrs and non-movable types)
    WindowUIContext() = default;
    ~WindowUIContext() = default;
    WindowUIContext(const WindowUIContext&) = delete;
    WindowUIContext& operator=(const WindowUIContext&) = delete;
    WindowUIContext(WindowUIContext&&) = delete;
    WindowUIContext& operator=(WindowUIContext&&) = delete;
};

}  // namespace spectra
