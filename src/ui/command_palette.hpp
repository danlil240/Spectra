#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <cmath>
    #include <string>
    #include <vector>

struct ImFont;

namespace spectra
{

class CommandRegistry;
class ShortcutManager;
struct CommandSearchResult;

// ImGui-based command palette overlay (Ctrl+K).
// Renders a centered floating search box with fuzzy-matched command results.
// Arrow keys navigate, Enter executes, Escape closes.
class CommandPalette
{
   public:
    CommandPalette() = default;
    ~CommandPalette() = default;

    CommandPalette(const CommandPalette&) = delete;
    CommandPalette& operator=(const CommandPalette&) = delete;

    // Set dependencies (not owned).
    void set_command_registry(CommandRegistry* registry) { registry_ = registry; }
    void set_shortcut_manager(ShortcutManager* shortcuts) { shortcuts_ = shortcuts; }

    // Open/close the palette.
    void open();
    void close();
    void toggle();
    bool is_open() const { return open_; }

    // Draw the palette UI. Call each frame inside an ImGui context.
    // Returns true if a command was executed this frame.
    bool draw(float window_width, float window_height);

    // Set fonts for rendering (optional — uses defaults if null).
    void set_body_font(ImFont* font) { font_body_ = font; }
    void set_heading_font(ImFont* font) { font_heading_ = font; }

   private:
    void update_search();
    bool handle_keyboard();

    CommandRegistry* registry_ = nullptr;
    ShortcutManager* shortcuts_ = nullptr;

    bool open_ = false;
    bool focus_input_ = false;  // Focus the input field next frame
    char search_buf_[256] = {};
    std::string last_query_;

    // Cached search results
    std::vector<CommandSearchResult> results_;
    int selected_index_ = 0;
    bool scroll_to_selected_ = false;  // Set true only on keyboard nav

    // Returns true when the palette is open and should consume all mouse input
    bool wants_mouse() const { return open_ && opacity_ > 0.01f; }

    // Animation
    float opacity_ = 0.0f;
    float scale_ = 0.98f;

    // Smooth scroll state
    float scroll_offset_ = 0.0f;      // Current smooth scroll position (pixels)
    float scroll_target_ = 0.0f;      // Target scroll position (pixels)
    float scroll_velocity_ = 0.0f;    // Inertial velocity (pixels/sec)
    float content_height_ = 0.0f;     // Total content height used for scroll math
    float visible_height_ = 0.0f;     // Visible region height
    float measured_overhead_ = 0.0f;  // Non-results space (input + separator + padding), measured
    float measured_content_ = 0.0f;   // Actual rendered content height, measured from ImGui cursor

    // Scrollbar state
    float scrollbar_opacity_ = 0.0f;  // Animated opacity (fades in on scroll, out on idle)
    float scrollbar_hover_t_ = 0.0f;  // Hover animation (widens on hover)
    bool scrollbar_dragging_ = false;
    float scrollbar_drag_offset_ = 0.0f;  // Offset from thumb top when drag started

    // Fonts
    ImFont* font_body_ = nullptr;
    ImFont* font_heading_ = nullptr;

    // Layout constants
    static constexpr float PALETTE_WIDTH = 560.0f;
    static constexpr float PALETTE_MAX_HEIGHT = 420.0f;
    static constexpr float RESULT_ITEM_HEIGHT = 36.0f;
    static constexpr float CATEGORY_HEADER_HEIGHT = 34.0f;
    static constexpr float INPUT_HEIGHT = 44.0f;
    static constexpr float ANIM_SPEED = 12.0f;           // Lerp speed for open/close
    static constexpr float SCROLL_SPEED = 50.0f;         // Pixels per scroll tick
    static constexpr float SCROLL_SMOOTHING = 14.0f;     // Exponential lerp rate
    static constexpr float SCROLL_DECEL = 8.0f;          // Velocity damping rate
    static constexpr float SCROLL_VEL_THRESHOLD = 0.5f;  // Stop threshold
};

}  // namespace spectra

#endif  // SPECTRA_USE_IMGUI
