#pragma once

// BagPlaybackPanel — ImGui scrub-bar panel for rosbag2 playback.
//
// Renders a transport-control toolbar + time scrub bar backed by a BagPlayer.
// When a TimelineEditor is attached to the BagPlayer, the panel draws a
// full timeline view showing per-topic activity bands.  Without a
// TimelineEditor, a compact horizontal progress bar with transport buttons
// is shown instead.
//
// Layout (compact mode — no TimelineEditor):
//
//   ┌──────────────────────────────────────────────────────────────┐
//   │ [◀◀] [▶/‖] [■] [◀] [▶]  0.0 / 120.3 s  [speed: 1.0×]      │
//   │ ████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │
//   └──────────────────────────────────────────────────────────────┘
//
// Layout (timeline mode — TimelineEditor attached):
//
//   ┌──────────────────────────────────────────────────────────────┐
//   │ [◀◀] [▶/‖] [■] [◀] [▶]  0.0 / 120.3 s  [speed: 1.0×] [⟳]  │
//   │ ┌────────────────────────────────────────────────────────┐   │
//   │ │ TimelineEditor::draw() — full scrub + activity bands   │   │
//   │ └────────────────────────────────────────────────────────┘   │
//   └──────────────────────────────────────────────────────────────┘
//
// Thread-safety:
//   draw() must be called from the render thread only.
//   All BagPlayer mutations (play/pause/seek/set_rate) are forwarded directly
//   to BagPlayer, which is thread-safe.
//
// ImGui dependency:
//   All rendering is gated behind SPECTRA_USE_IMGUI.
//   Pure-logic helpers (format_time, rate_label) are always compiled.

#include <cstdint>
#include <string>

namespace spectra::adapters::ros2
{
class BagPlayer;
}

namespace spectra
{
class TimelineEditor;
}

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// BagPlaybackPanel
// ---------------------------------------------------------------------------

class BagPlaybackPanel
{
public:
    // Construct.
    // player — must outlive the panel; may be nullptr (panel shows empty state).
    explicit BagPlaybackPanel(BagPlayer* player = nullptr);
    ~BagPlaybackPanel() = default;

    BagPlaybackPanel(const BagPlaybackPanel&)            = delete;
    BagPlaybackPanel& operator=(const BagPlaybackPanel&) = delete;

    // ------------------------------------------------------------------
    // Player wiring
    // ------------------------------------------------------------------

    void      set_player(BagPlayer* p);
    BagPlayer* player() const { return player_; }

    // ------------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------------

    // Panel title shown in ImGui::Begin().
    void               set_title(const std::string& t) { title_ = t; }
    const std::string& title() const                   { return title_; }

    // Height of the compact progress bar in pixels (used when no TimelineEditor).
    void  set_progress_bar_height(float h) { progress_bar_height_ = h; }
    float progress_bar_height() const      { return progress_bar_height_; }

    // Height of the TimelineEditor area in pixels (used when TimelineEditor present).
    void  set_timeline_height(float h) { timeline_height_ = h; }
    float timeline_height() const      { return timeline_height_; }

    // Show the loop toggle button.
    void set_show_loop_button(bool v) { show_loop_button_ = v; }

    // Show the rate slider (allows 0.1×–10×).
    void set_show_rate_slider(bool v) { show_rate_slider_ = v; }

    // ------------------------------------------------------------------
    // Drawing
    // ------------------------------------------------------------------

#ifdef SPECTRA_USE_IMGUI
    // Draw the panel as a standalone ImGui window.
    // p_open — if non-null, a close button is shown; set to false to close.
    void draw(bool* p_open = nullptr);

    // Draw only the panel contents inline (no ImGui::Begin/End wrapper).
    // Use this when embedding inside an existing window.
    void draw_inline();
#endif

    // ------------------------------------------------------------------
    // Static formatting helpers (always compiled for tests)
    // ------------------------------------------------------------------

    // Format seconds as "M:SS.s" or "H:MM:SS" for display.
    static std::string format_time(double sec);

    // Format rate as "0.5×", "1.0×", "2.0×".
    static std::string rate_label(double rate);

private:
#ifdef SPECTRA_USE_IMGUI
    void draw_toolbar();
    void draw_progress_bar();
    void draw_timeline();
    void draw_status_line();
#endif

    BagPlayer*  player_{nullptr};
    std::string title_{"Bag Playback"};

    float progress_bar_height_{12.0f};
    float timeline_height_{160.0f};

    bool show_loop_button_{true};
    bool show_rate_slider_{true};

    // Rate slider state (local to avoid ImGui id conflicts).
    float rate_slider_{1.0f};
    bool  rate_slider_dirty_{false};
};

}   // namespace spectra::adapters::ros2
