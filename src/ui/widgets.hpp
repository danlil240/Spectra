#pragma once

#ifdef PLOTIX_USE_IMGUI

#include <plotix/color.hpp>
#include "theme.hpp"
#include <string>

struct ImFont;

namespace plotix::ui::widgets {

// Section header with collapsible state and smooth chevron animation.
// Returns true if the section is open.
bool section_header(const char* label, bool* open, ImFont* font = nullptr);

// Horizontal separator with theme-aware color
void separator();

// Read-only info row: "Label    Value" (label left-aligned, value right-aligned)
void info_row(const char* label, const char* value);

// Monospace info row for numeric data
void info_row_mono(const char* label, const char* value);

// Color picker field with inline swatch + label
bool color_field(const char* label, plotix::Color& color);

// Float slider with label
bool slider_field(const char* label, float& value, float min, float max,
                  const char* fmt = "%.2f");

// Float drag field with label
bool drag_field(const char* label, float& value, float speed = 0.5f,
                float min = 0.0f, float max = 0.0f, const char* fmt = "%.1f");

// Two-component float drag (e.g. axis limits)
bool drag_field2(const char* label, float& v0, float& v1, float speed = 0.01f,
                 const char* fmt = "%.3f");

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
bool icon_button_small(const char* icon, const char* tooltip = nullptr,
                       bool active = false);

// Indented group (pushes indent + draws subtle left border)
void begin_group(const char* id);
void end_group();

// Color swatch (small inline preview, no picker)
void color_swatch(const plotix::Color& color, float size = 14.0f);

// Spacing helpers
void small_spacing();
void section_spacing();

} // namespace plotix::ui::widgets

#endif // PLOTIX_USE_IMGUI
