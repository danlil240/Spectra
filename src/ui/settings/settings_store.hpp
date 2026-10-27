#pragma once

#include <functional>
#include <string>

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
    bool        inspector_visible    = true;
    bool        nav_rail_visible     = true;
    bool        timeline_visible     = false;
};

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
