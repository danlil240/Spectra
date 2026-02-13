#pragma once

#ifdef PLOTIX_USE_IMGUI

#include <string>
#include <vector>

struct ImFont;

namespace plotix {

class CommandRegistry;
class ShortcutManager;
struct CommandSearchResult;

// ImGui-based command palette overlay (Ctrl+K).
// Renders a centered floating search box with fuzzy-matched command results.
// Arrow keys navigate, Enter executes, Escape closes.
class CommandPalette {
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

    // Animation
    float opacity_ = 0.0f;
    float scale_ = 0.98f;

    // Fonts
    ImFont* font_body_ = nullptr;
    ImFont* font_heading_ = nullptr;

    // Layout constants
    static constexpr float PALETTE_WIDTH = 560.0f;
    static constexpr float PALETTE_MAX_HEIGHT = 420.0f;
    static constexpr float RESULT_ITEM_HEIGHT = 36.0f;
    static constexpr float INPUT_HEIGHT = 44.0f;
    static constexpr float ANIM_SPEED = 12.0f;  // Lerp speed for open/close
};

} // namespace plotix

#endif // PLOTIX_USE_IMGUI
