#pragma once

#include <string>

namespace spectra
{
class ShortcutConfig;
}

namespace spectra::ui
{
class ThemeManager;
}

namespace spectra::ui::settings
{

class SettingsStore;

class SettingsPanel
{
   public:
    void set_settings_store(SettingsStore* store) { store_ = store; }
    void set_theme_manager(ui::ThemeManager* tm) { theme_mgr_ = tm; }
    void set_shortcut_config(ShortcutConfig* sc) { shortcut_cfg_ = sc; }

    bool is_visible() const { return visible_; }
    void set_visible(bool v) { visible_ = v; }
    void open() { visible_ = true; }

    // Programmatically select a tab: 0=Appearance, 1=Shortcuts, 2=UI Defaults.
    // Safe to call from automation — takes effect on the next draw() call.
    void select_tab(int idx) { pending_tab_ = idx; }

    void draw();

   private:
    void draw_appearance_tab();
    void draw_shortcuts_tab();
    void draw_ui_defaults_tab();

    SettingsStore*    store_           = nullptr;
    ui::ThemeManager* theme_mgr_       = nullptr;
    ShortcutConfig*   shortcut_cfg_    = nullptr;
    bool              visible_         = false;
    int               pending_tab_     = -1;   // tab to switch to (-1 = no pending switch)
    char              filter_buf_[128] = {};

    bool        capturing_shortcut_ = false;
    std::string capturing_command_id_;
};

}   // namespace spectra::ui::settings
