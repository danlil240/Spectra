#pragma once

#include <string>

#include "settings_layout.hpp"

struct ImFont;

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
    // SemiBold fonts for section headings (nullptr: the current font).
    void set_fonts(ImFont* section) { font_section_ = section; }

    bool is_visible() const { return visible_; }
    void set_visible(bool v) { visible_ = v; }
    void open() { visible_ = true; }

    // Programmatically select a tab: 0=Appearance, 1=Shortcuts, 2=UI Defaults.
    // Safe to call from automation — takes effect on the next draw() call.
    void select_tab(int idx) { pending_tab_ = idx; }
    // Tab whose body was drawn last frame (0=Appearance, 1=Shortcuts, 2=UI Defaults).
    int active_tab() const { return active_tab_; }

    void draw();

    // Text of the search box above the tabs (empty: tabs are shown).
    const std::string& search_query() const { return search_; }
    void               set_search_query(std::string q) { search_ = std::move(q); }

   private:
    // One searchable block of a tab.  With a search query every matching
    // section of every tab is drawn in one list instead of the tabs.
    struct Section
    {
        int         tab;
        const char* title;
        const char* keywords;
        void (SettingsPanel::*draw)();
    };
    static const Section* sections(size_t& count);

    void draw_section_heading(const char* title);
    void draw_search_results();
    void draw_appearance_tab();
    void draw_theme_section();
    void draw_palette_section();
    void draw_cvd_section();
    void draw_scale_section();
    void draw_files_section();
    void draw_glass_section();
    void draw_appearance_footer();
    void draw_shortcuts_tab();
    void draw_shortcuts_footer();
    void draw_ui_defaults_tab();
    bool shortcuts_match(const std::string& lowered_query) const;

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
    // Natural tab-body height per tab (+ search results), measured when last
    // shown (-1 = unknown).
    float body_content_h_[4] = {-1.0f, -1.0f, -1.0f, -1.0f};

    ImFont*     font_section_ = nullptr;
    std::string search_;
    // Result of the last import / export / reload in the Files section.
    std::string files_status_;
    bool        files_status_error_ = false;
    std::string last_export_dir_;   // folder of the last theme / palette export

    bool        capturing_shortcut_ = false;
    std::string capturing_command_id_;
};

}   // namespace spectra::ui::settings
