#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "ui/settings/export_settings.hpp"

namespace spectra::ui
{
class ThemeManager;
}

namespace spectra::ui::settings
{

struct SettingsData
{
    std::string default_theme        = "night";
    std::string default_data_palette = "default";
    // Last dark / light theme used, for "Toggle Dark/Light Theme".
    std::string last_dark_theme   = "night";
    std::string last_light_theme  = "light";
    bool        inspector_visible = true;
    bool        nav_rail_visible  = true;
    bool        timeline_visible  = false;
    // App UI scale (1.0 = 100%); clamped to ThemeManager::UI_SCALE_MIN..MAX.
    float ui_scale = 1.0f;
    // Transforms menu: true replaces the series data in place, false (default,
    // non-destructive) adds a transformed copy next to each source series.
    bool transform_overwrite = false;
    // Data files (CSV, ULog, MCAP, ...) opened or dropped most recently, newest
    // first, absolute paths; shown on the start page.  See push_recent_file().
    std::vector<std::string> recent_files;

    // Last choices of the Export options dialog (file.export_dialog).
    ExportDialogSettings export_dialog;
};

// Longest SettingsData::recent_files list.
inline constexpr size_t MAX_RECENT_FILES = 8;

// Moves @p path (made absolute and lexically normal) to the front of @p list,
// dropping duplicates and anything beyond @p max_entries.  Empty paths are ignored.
void push_recent_file(std::vector<std::string>& list,
                      const std::string&        path,
                      size_t                    max_entries = MAX_RECENT_FILES);
// Removes @p path (as given or normalized) from @p list.
void remove_recent_file(std::vector<std::string>& list, const std::string& path);

// Theme and data palette an App starts with.
struct StartupTheme
{
    std::string theme;
    std::string data_palette;
};

// Resolve the startup theme/palette.  Precedence: explicit (non-empty)
// @p theme / @p data_palette, then @p user settings when non-null, then the
// built-in defaults (SettingsData{}).  Pure; does not touch the filesystem.
StartupTheme resolve_startup_theme(const SettingsData* user,
                                   const std::string&  theme,
                                   const std::string&  data_palette);

// Whether an App should read the user's settings file: windowed apps always
// do; headless apps only when explicitly asked, so exports are reproducible.
inline bool reads_user_settings(bool headless, bool use_user_settings)
{
    return !headless || use_user_settings;
}

class SettingsStore;

// What Settings -> Appearance shows.  The combos display the *active* theme
// and data palette (ThemeManager), which commands, undo/redo and automation
// may change without touching the saved startup default.  When the two
// differ the panel shows a secondary "Startup default: ..." hint.
struct AppearanceState
{
    std::string active_theme;
    std::string startup_theme;
    std::string active_data_palette;
    std::string startup_data_palette;

    bool theme_matches_startup() const { return active_theme == startup_theme; }
    bool data_palette_matches_startup() const
    {
        return active_data_palette == startup_data_palette;
    }
};

AppearanceState appearance_state(const ui::ThemeManager& tm, const SettingsData& d);

// Settings-panel selection: makes @p theme active and saves it as the
// startup default (notifies @p store).  Returns false when the theme is not
// registered, in which case nothing changes.
bool select_theme(ui::ThemeManager& tm, SettingsStore& store, const std::string& theme);
bool select_data_palette(ui::ThemeManager& tm, SettingsStore& store, const std::string& palette);

// Save the currently active theme / data palette as the startup default.
void make_active_theme_default(const ui::ThemeManager& tm, SettingsStore& store);
void make_active_data_palette_default(const ui::ThemeManager& tm, SettingsStore& store);

// Copy the theme manager's last dark / light theme into @p store; notifies
// (saves) only when they changed.  Returns true when they changed.
bool remember_toggle_themes(const ui::ThemeManager& tm, SettingsStore& store);

class SettingsStore
{
   public:
    SettingsStore()  = default;
    ~SettingsStore() = default;

    SettingsStore(const SettingsStore&)            = delete;
    SettingsStore& operator=(const SettingsStore&) = delete;

    bool load();
    bool load(const std::string& path);

    bool save() const;
    bool save(const std::string& path) const;

    const SettingsData& data() const { return data_; }
    SettingsData&       data_mut() { return data_; }

    void apply_to(ui::ThemeManager& tm) const;

    // Recent data files (start page).  Each change notifies listeners, which
    // save the file in windowed apps.
    void add_recent_file(const std::string& path);
    void forget_recent_file(const std::string& path);
    void clear_recent_files();

    static std::string default_path();

    using ChangeCallback = std::function<void()>;
    void set_on_change(ChangeCallback cb) { on_change_ = std::move(cb); }
    void notify_change();

   private:
    SettingsData   data_;
    ChangeCallback on_change_;
    std::string    loaded_path_;
};

}   // namespace spectra::ui::settings
