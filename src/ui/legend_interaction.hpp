#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <cstddef>
    #include <spectra/color.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/series.hpp>
    #include <unordered_map>

struct ImFont;

namespace spectra
{

class TransitionEngine;

// Per-series animation state for legend interaction.
struct LegendSeriesState
{
    float opacity = 1.0f;         // Current animated opacity (0=hidden, 1=visible)
    float target_opacity = 1.0f;  // Target opacity for animation
    bool user_visible = true;     // User-toggled visibility state
};

// Legend interaction: click-to-toggle series visibility with animated opacity,
// drag-to-reposition the legend box.
class LegendInteraction
{
   public:
    LegendInteraction() = default;

    // Set fonts for legend rendering
    void set_fonts(ImFont* body, ImFont* icon);

    // Set the transition engine for smooth opacity animations (optional)
    void set_transition_engine(TransitionEngine* te) { transition_engine_ = te; }

    // ─── Per-frame update ───────────────────────────────────────────────

    // Update animation states. Call once per frame.
    void update(float dt, Figure& figure);

    // ─── Drawing ────────────────────────────────────────────────────────

    // Draw the interactive legend overlay for the given axes.
    // Returns true if the legend consumed a mouse event this frame.
    bool draw(Axes& axes, const Rect& viewport, size_t axes_index);

    // ─── Queries ────────────────────────────────────────────────────────

    // Get the effective opacity for a series (for use by the renderer).
    float series_opacity(const Series* series) const;

    // Check if a series is toggled visible by the user.
    bool is_series_visible(const Series* series) const;

    // ─── Configuration ──────────────────────────────────────────────────

    // Enable/disable legend dragging
    void set_draggable(bool d) { draggable_ = d; }
    bool draggable() const { return draggable_; }

    // Enable/disable click-to-toggle
    void set_toggleable(bool t) { toggleable_ = t; }
    bool toggleable() const { return toggleable_; }

    // Animation duration for opacity transitions (seconds)
    void set_toggle_duration(float d) { toggle_duration_ = d; }
    float toggle_duration() const { return toggle_duration_; }

   private:
    // Get or create state for a series pointer
    LegendSeriesState& get_state(const Series* s);

    // Per-axes legend position offset (from default position)
    struct LegendOffset
    {
        float dx = 0.0f;
        float dy = 0.0f;
    };

    LegendOffset& get_offset(size_t axes_index);

    // Series state keyed by raw pointer (valid for the lifetime of the figure)
    std::unordered_map<const Series*, LegendSeriesState> series_states_;

    // Per-axes drag offset
    std::unordered_map<size_t, LegendOffset> legend_offsets_;

    // Drag state
    bool dragging_ = false;
    float drag_start_mx_ = 0.0f;
    float drag_start_my_ = 0.0f;
    float drag_start_ox_ = 0.0f;
    float drag_start_oy_ = 0.0f;
    size_t drag_axes_index_ = 0;

    // Fonts
    ImFont* font_body_ = nullptr;
    ImFont* font_icon_ = nullptr;

    // Configuration
    bool draggable_ = true;
    bool toggleable_ = true;
    float toggle_duration_ = 0.2f;

    // External systems
    TransitionEngine* transition_engine_ = nullptr;
};

}  // namespace spectra

#endif  // SPECTRA_USE_IMGUI
