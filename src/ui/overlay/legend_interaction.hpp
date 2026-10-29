#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <cstddef>
    #include <cstdint>
    #include <spectra/color.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/series.hpp>
    #include <span>
    #include <string>
    #include <unordered_map>
    #include <vector>

struct ImFont;
struct ImVec2;

namespace spectra::ui
{
class ThemeManager;
struct ThemeColors;
}   // namespace spectra::ui

namespace spectra
{

class TransitionEngine;

// Per-series animation state for legend interaction.
struct LegendSeriesState
{
    float opacity        = 1.0f;   // Current animated opacity (0=hidden, 1=visible)
    float target_opacity = 1.0f;   // Target opacity for animation
    bool  user_visible   = true;   // User-toggled visibility state
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
    // figure_id disambiguates state across figures in split mode.
    // Returns true if the legend consumed a mouse event this frame.
    bool draw(AxesBase&           axes,
              const Rect&         viewport,
              size_t              axes_index,
              const LegendConfig& config,
              uintptr_t           figure_id,
              const FigureStyle*  figure_style = nullptr);

    // Drop all per-(figure, axes) state of a destroyed figure (drag offsets,
    // Best-corner hysteresis, overflow cells) and return its legend ImGui
    // window names to the pool for reuse.
    void forget_figure(uintptr_t figure_id);

    // Number of distinct legend ImGui windows ever named (pool size); bounded
    // by the number of legends visible at once, not by figures ever created.
    size_t legend_window_pool_size() const { return next_window_slot_; }

    // ImGui window name of the legend of (figure, axes) ("##legend_<slot>"),
    // or "" when that legend has not been drawn since the figure was forgotten.
    std::string window_name(uintptr_t figure_id, size_t axes_index) const;

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
    void  set_toggle_duration(float d) { toggle_duration_ = d; }
    float toggle_duration() const { return toggle_duration_; }

    void set_theme_manager(ui::ThemeManager* tm) { theme_mgr_ = tm; }

    // ─── "+N more" cell (summarised legends) ────────────────────────────

    // State of the "+N more" cell drawn by the last draw() of a legend, for
    // hit-testing and tests.  Hovering the cell lists the hidden entries (with
    // colour keys); clicking it opens the full legend popup, whose rows toggle
    // series visibility like the legend's own rows.  The mouse wheel over
    // the legend scrolls the run of entries the box shows (`first`).
    struct OverflowCell
    {
        Rect        cell;                 // screen pixels
        int         shown  = 0;           // entries drawn in the legend box
        int         hidden = 0;           // entries summarised by the cell
        int         first  = 0;           // first entry drawn (mouse-wheel scroll offset)
        std::string tooltip_first;        // first hidden label listed by the hover tooltip
        bool        hovered    = false;   // tooltip shown this frame
        bool        popup_open = false;   // full legend popup shown this frame
        int         popup_rows = 0;       // rows submitted to the popup list
        Rect        popup_list;           // screen rect of the popup's (scrolling) list
    };
    // nullptr when that legend has no "+N more" cell.
    const OverflowCell* overflow_cell(uintptr_t figure_id, size_t axes_index) const
    {
        auto it = overflow_cells_.find(make_offset_key(figure_id, axes_index));
        return it != overflow_cells_.end() ? &it->second : nullptr;
    }

   private:
    // Get or create state for a series pointer
    LegendSeriesState& get_state(const Series* s);

    // Per-axes legend position offset (from default position)
    struct LegendOffset
    {
        float dx = 0.0f;
        float dy = 0.0f;
    };

    // Composite key: (figure_id, axes_index) to prevent cross-figure state leaks
    using OffsetKey = uint64_t;
    static OffsetKey make_offset_key(uintptr_t fig_id, size_t ax_idx)
    {
        return (static_cast<uint64_t>(fig_id) << 16) ^ static_cast<uint64_t>(ax_idx);
    }
    LegendOffset& get_offset(uintptr_t figure_id, size_t axes_index);

    // Shared look of one legend row (legend box and full legend popup).
    struct RowStyle
    {
        ImFont*                font       = nullptr;
        float                  font_size  = 0.0f;
        float                  row_height = 0.0f;
        float                  label_w    = 0.0f;   // label budget before ellipsizing
        Color                  ink;                 // label colour
        const ui::ThemeColors* colors = nullptr;
    };
    // Swatch + gap (+ visibility eye) width in front of / after a row's label.
    float row_reserved_width() const;
    // One clickable entry row (swatch, label, eye) at screen position @p pos,
    // @p width wide.  Clicking toggles the series.  Returns true if clicked.
    bool draw_entry_row(Series&         s,
                        const ImVec2&   pos,
                        float           width,
                        const RowStyle& style,
                        const char*     id);
    // The "+N more" cell at @p pos, its hover tooltip and the full legend
    // popup it opens.  Returns true if it consumed a click.
    bool draw_overflow_cell(std::span<Series* const> entries,
                            int                      first,
                            int                      shown,
                            const ImVec2&            pos,
                            float                    width,
                            const RowStyle&          style,
                            const Rect&              legend_box,
                            uint64_t                 key);
    // Flip a series' visibility through the legend (animated dimming).
    void toggle_series(Series& s);

    // Series state keyed by raw pointer (valid for the lifetime of the figure)
    std::unordered_map<const Series*, LegendSeriesState> series_states_;

    // Per-(figure, axes) drag offset
    std::unordered_map<OffsetKey, LegendOffset> legend_offsets_;

    // Last corner chosen for LegendPosition::Best, per (figure, axes) — gives
    // the auto-placement hysteresis so the legend doesn't jitter.
    std::unordered_map<OffsetKey, LegendPosition> best_corner_;

    // "+N more" cell per (figure, axes), as drawn by the last draw().
    std::unordered_map<OffsetKey, OverflowCell> overflow_cells_;

    // Mouse-wheel scroll offset (in entries) of a summarised legend, per
    // (figure, axes); see legend_layout::scroll_offset.
    std::unordered_map<OffsetKey, float> scroll_offsets_;
    // Legend ImGui window name slot per (figure, axes).  ImGui never frees a
    // window, so naming windows by figure address leaked one per figure ever
    // shown; slots of forgotten figures are reused instead.
    struct WindowSlot
    {
        uintptr_t figure_id = 0;
        size_t    slot      = 0;
    };
    std::unordered_map<OffsetKey, WindowSlot> window_slots_;
    std::vector<size_t>                       free_window_slots_;
    size_t                                    next_window_slot_ = 0;
    // Returns the window slot of (figure, axes); `fresh` is set when the slot
    // was just (re)assigned, so stale ImGui window state must be reset.
    size_t window_slot(uintptr_t figure_id, size_t axes_index, bool& fresh);

    // Drag state
    bool   dragging_        = false;
    float  drag_start_mx_   = 0.0f;
    float  drag_start_my_   = 0.0f;
    float  drag_start_ox_   = 0.0f;
    float  drag_start_oy_   = 0.0f;
    size_t drag_axes_index_ = 0;

    // Fonts
    ImFont* font_body_ = nullptr;
    ImFont* font_icon_ = nullptr;

    // Configuration
    bool  draggable_       = true;
    bool  toggleable_      = true;
    float toggle_duration_ = 0.2f;

    // External systems
    TransitionEngine* transition_engine_ = nullptr;

    ui::ThemeManager* theme_mgr_ = nullptr;
};

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
