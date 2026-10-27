#pragma once

/// @file theme_api.hpp
/// @brief Public read-only theme API for plugins and embedders.
///
/// Provides a stable, minimal interface for querying current theme colors
/// without exposing internal ThemeManager details.

#include <cstdint>

namespace spectra
{

/// Snapshot of the current theme colors, suitable for passing to plugins
/// or external code that needs to render with matching colors.
struct ThemeSnapshot
{
    // Surfaces
    float bg_canvas[4];      // Plot area background (RGBA)
    float bg_primary[4];     // Window background
    float bg_secondary[4];   // Panel background
    float bg_elevated[4];    // Tooltip / popup background

    // Text
    float text_primary[4];
    float text_secondary[4];

    // Borders
    float border_default[4];
    float border_subtle[4];

    // Accent
    float accent[4];
    float accent_hover[4];

    // Semantic
    float success[4];
    float warning[4];
    float error[4];

    // Plot
    float grid_major[4];
    float grid_minor[4];
    float axis_line[4];
    float crosshair[4];

    // Metadata
    uint32_t version;   // Monotonically increasing; compare to detect changes
};

/// Fill a ThemeSnapshot with the current theme colors.
/// Returns the theme version counter.
uint32_t get_theme_snapshot(ThemeSnapshot& out);

/// Register a callback invoked whenever the theme colors change.
/// The callback receives the new snapshot.  Pass nullptr to unregister.
///
/// Semantics:
/// - Fires after a theme switch has been fully applied (ThemeManager::set_theme,
///   including the end of an animated transition).  Intermediate transition
///   frames are not reported; poll get_theme_snapshot() if you need them.
/// - Fires only when the snapshot colors differ from the last snapshot reported
///   for that theme manager, so re-applying the current theme does not call
///   it again.  Changes that do not alter any snapshot field (data palette,
///   glass/translucency settings) do not fire it.
/// - Called synchronously on the thread that applied the change (the UI
///   thread), after the new colors are in effect.  Do not block in it.
/// - The registration is process-wide: one callback at a time (registering
///   replaces the previous one), shared by every Spectra app instance in the
///   process.  The snapshot passed in describes the instance that changed.
/// - The function must stay callable until it is unregistered with nullptr.
///   Once register_theme_change_callback(nullptr) returns, no new calls start.
using ThemeChangeCallback = void (*)(const ThemeSnapshot&);
void register_theme_change_callback(ThemeChangeCallback callback);

}   // namespace spectra
