#pragma once

#include <string>

#include "settings_layout.hpp"

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
    void draw_appearance_footer();
    void draw_shortcuts_tab();
    void draw_shortcuts_footer();
    void draw_ui_defaults_tab();

    SettingsStore*    store_           = nullptr;
    ui::ThemeManager* theme_mgr_       = nullptr;
    ShortcutConfig*   shortcut_cfg_    = nullptr;
    bool              visible_         = false;
    int               pending_tab_     = -1;     // tab to switch to (-1 = no pending switch)
    int               active_tab_      = 0;      // tab whose body is drawn
    int               recenter_frames_ = 0;      // frames left to re-center after opening
    float             body_max_h_      = 0.0f;   // height budget of the scrolling tab body
    float             body_origin_y_   = 0.0f;   // cursor y where the tab body starts
    char              filter_buf_[128] = {};
    SettingsVec2      last_pos_;   // window rect last frame
    SettingsVec2      last_size_;
    SettingsVec2      last_work_size_;
    // Natural tab-body height per tab, measured when last shown (-1 = unknown).
    float body_content_h_[3] = {-1.0f, -1.0f, -1.0f};

    bool        capturing_shortcut_ = false;
    std::string capturing_command_id_;
};

}   // namespace spectra::ui::settings
