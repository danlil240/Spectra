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
