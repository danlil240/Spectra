#pragma once

// Dear ImGui conversions for theme colours.
//
// UI code should take its colours from ThemeManager semantic tokens
// (ui::theme().text_primary, .warning, .error, ...) instead of hard-coding
// IM_COL32(...) / ImVec4(...) literals, so that panels stay readable in every
// theme (dark, night, light, high_contrast).  tools/check_color_literals.py
// enforces this in CI.
//
// Include only from translation units that already depend on Dear ImGui.

#include <imgui.h>

#include "theme.hpp"

namespace spectra::ui
{

inline ImVec4 to_imvec4(const Color& c)
{
    return ImVec4(c.r, c.g, c.b, c.a);
}

// Same colour with its alpha replaced.
inline ImVec4 to_imvec4(const Color& c, float alpha)
{
    return ImVec4(c.r, c.g, c.b, alpha);
}

inline ImU32 to_imu32(const Color& c)
{
    return ImGui::ColorConvertFloat4ToU32(to_imvec4(c));
}

// Same colour with its alpha replaced.
inline ImU32 to_imu32(const Color& c, float alpha)
{
    return ImGui::ColorConvertFloat4ToU32(to_imvec4(c, alpha));
}

}   // namespace spectra::ui
