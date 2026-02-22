#pragma once

namespace spectra::ui::tokens
{

// Spacing Scale (base: 4px)
constexpr float SPACE_0  = 0.0f;    // No space
constexpr float SPACE_1  = 4.0f;    // Tight
constexpr float SPACE_2  = 8.0f;    // Compact
constexpr float SPACE_3  = 12.0f;   // Default
constexpr float SPACE_4  = 16.0f;   // Comfortable
constexpr float SPACE_5  = 20.0f;   // Spacious
constexpr float SPACE_6  = 24.0f;   // Section gap
constexpr float SPACE_8  = 32.0f;   // Panel padding
constexpr float SPACE_10 = 40.0f;   // Zone gap
constexpr float SPACE_12 = 48.0f;   // Large gap
constexpr float SPACE_16 = 64.0f;   // Extra large gap

// Radius Scale
constexpr float RADIUS_SM   = 4.0f;     // Buttons, inputs
constexpr float RADIUS_MD   = 8.0f;     // Cards, panels
constexpr float RADIUS_LG   = 12.0f;    // Modals, floating panels
constexpr float RADIUS_XL   = 16.0f;    // Main panels
constexpr float RADIUS_PILL = 999.0f;   // Pill buttons, badges

// Font Scale (Inter)
constexpr float FONT_XS   = 11.0f;   // Status bar, badges
constexpr float FONT_SM   = 12.0f;   // Secondary text, labels
constexpr float FONT_BASE = 14.0f;   // Body text, controls
constexpr float FONT_MD   = 15.0f;   // Menu items
constexpr float FONT_LG   = 16.0f;   // Section headers
constexpr float FONT_XL   = 18.0f;   // Panel titles
constexpr float FONT_2XL  = 20.0f;   // Page titles
constexpr float FONT_MONO = 13.0f;   // Data readout, coordinates

// Layout Constants
constexpr float COMMAND_BAR_HEIGHT      = 48.0f;
constexpr float NAV_RAIL_WIDTH          = 48.0f;
constexpr float NAV_RAIL_WIDTH_EXPANDED = 200.0f;
constexpr float INSPECTOR_WIDTH         = 320.0f;
constexpr float INSPECTOR_WIDTH_MIN     = 240.0f;
constexpr float INSPECTOR_WIDTH_MAX     = 480.0f;
constexpr float STATUS_BAR_HEIGHT       = 28.0f;
constexpr float FLOATING_TOOLBAR_HEIGHT = 48.0f;

// Animation Durations (in seconds)
constexpr float DURATION_INSTANT = 0.0f;
constexpr float DURATION_FAST    = 0.1f;
constexpr float DURATION_NORMAL  = 0.15f;
constexpr float DURATION_SLOW    = 0.2f;
constexpr float DURATION_SLOWER  = 0.3f;

// Icon Sizes
constexpr float ICON_XS = 12.0f;
constexpr float ICON_SM = 16.0f;
constexpr float ICON_MD = 20.0f;
constexpr float ICON_LG = 24.0f;
constexpr float ICON_XL = 32.0f;

// Border Widths
constexpr float BORDER_WIDTH_THIN   = 0.5f;
constexpr float BORDER_WIDTH_NORMAL = 1.0f;
constexpr float BORDER_WIDTH_THICK  = 2.0f;

// Opacity Values
constexpr float OPACITY_HIDDEN  = 0.0f;
constexpr float OPACITY_FAINT   = 0.1f;
constexpr float OPACITY_SUBTLE  = 0.3f;
constexpr float OPACITY_MID     = 0.5f;
constexpr float OPACITY_STRONG  = 0.7f;
constexpr float OPACITY_VISIBLE = 0.9f;
constexpr float OPACITY_OPAQUE  = 1.0f;

// Shadow Intensity
constexpr float SHADOW_INTENSITY_SUBTLE   = 0.12f;
constexpr float SHADOW_INTENSITY_NORMAL   = 0.15f;
constexpr float SHADOW_INTENSITY_STRONG   = 0.20f;
constexpr float SHADOW_INTENSITY_STRONGER = 0.25f;

// Interaction Values
constexpr float HOVER_DISTANCE_THRESHOLD    = 8.0f;    // For data point snapping
constexpr float DRAG_THRESHOLD              = 4.0f;    // Minimum drag distance
constexpr float DOUBLE_CLICK_TIME           = 0.5f;    // Seconds between clicks
constexpr float TOOLTIP_DELAY               = 0.05f;   // Seconds before tooltip appears
constexpr float FLOATING_TOOLBAR_HIDE_DELAY = 2.0f;    // Seconds of inactivity before hiding

// Performance Targets
constexpr float FRAME_TIME_BUDGET_MS    = 16.67f;   // 60 FPS
constexpr float UI_FRAME_TIME_TARGET_MS = 2.0f;     // UI rendering budget
constexpr float TOOLTIP_TIME_TARGET_MS  = 0.1f;     // Tooltip query budget

}   // namespace spectra::ui::tokens
