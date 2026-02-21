#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <span>
    #include <spectra/color.hpp>
    #include <string>
    #include <unordered_map>

    #include "theme.hpp"

struct ImFont;

namespace spectra::ui::widgets
{

// ─── Section Animation State ─────────────────────────────────────────────────
// Tracks per-section animation progress for smooth collapse/expand.

struct SectionAnimState
{
    float anim_t = 1.0f;  // 0 = collapsed, 1 = expanded
    bool target_open = true;
    bool was_open = true;  // Previous frame's open state
};

// Global section animation registry (keyed by section label pointer or ID).
// Call update_section_animations() once per frame to advance all animations.
void update_section_animations(float dt);
SectionAnimState& get_section_anim(const char* id);

// Section header with collapsible state and smooth chevron animation.
// Returns true if the section is open (content should be drawn).
// When animated=true, content fades in/out with smooth height clipping.
bool section_header(const char* label, bool* open, ImFont* font = nullptr);

// Begin/end animated section content. Call after section_header returns true.
// begin_animated_section returns false if the section is fully collapsed
// (caller can skip drawing). end_animated_section must always be called if
// begin returned true.
bool begin_animated_section(const char* id);
void end_animated_section();

// Horizontal separator with theme-aware color
void separator();

// Read-only info row: "Label    Value" (label left-aligned, value right-aligned)
void info_row(const char* label, const char* value);

// Monospace info row for numeric data
void info_row_mono(const char* label, const char* value);

// Color picker field with inline swatch + label
bool color_field(const char* label, spectra::Color& color);

// Float slider with label
bool slider_field(const char* label, float& value, float min, float max, const char* fmt = "%.2f");

// Float drag field with label
bool drag_field(const char* label,
                float& value,
                float speed = 0.5f,
                float min = 0.0f,
                float max = 0.0f,
                const char* fmt = "%.1f");

// Two-component float drag (e.g. axis limits)
bool drag_field2(
    const char* label, float& v0, float& v1, float speed = 0.01f, const char* fmt = "%.3f");

// Checkbox with theme styling
bool checkbox_field(const char* label, bool& value);

// Toggle switch (visual alternative to checkbox)
bool toggle_field(const char* label, bool& value);

// Combo dropdown
bool combo_field(const char* label, int& current, const char* const* items, int count);

// Text input field
bool text_field(const char* label, std::string& value);

// Button spanning full width
bool button_field(const char* label);

// Small inline icon button
bool icon_button_small(const char* icon, const char* tooltip = nullptr, bool active = false);

// Indented group (pushes indent + draws subtle left border)
void begin_group(const char* id);
void end_group();

// Color swatch (small inline preview, no picker)
void color_swatch(const spectra::Color& color, float size = 14.0f);

// Spacing helpers
void small_spacing();
void section_spacing();

// ─── New Widgets (Week 6) ────────────────────────────────────────────────────

// Sparkline: inline mini line chart for data preview
void sparkline(const char* id,
               std::span<const float> values,
               float width = -1.0f,
               float height = 32.0f,
               const spectra::Color& color = {});

// Progress bar with label
void progress_bar(const char* label, float fraction, const char* overlay = nullptr);

// Badge / tag (small colored pill with text)
void badge(const char* text, const spectra::Color& bg = {}, const spectra::Color& fg = {});

// Labeled separator (centered text in a horizontal line)
void separator_label(const char* label, ImFont* font = nullptr);

// Integer drag field
bool int_drag_field(
    const char* label, int& value, int speed = 1, int min = 0, int max = 0, const char* fmt = "%d");

// Stat row: label + value + optional unit, with monospace value
void stat_row(const char* label, const char* value, const char* unit = nullptr);

// Stat row with color indicator dot
void stat_row_colored(const char* label,
                      const char* value,
                      const spectra::Color& dot_color,
                      const char* unit = nullptr);

}  // namespace spectra::ui::widgets

#endif  // SPECTRA_USE_IMGUI
